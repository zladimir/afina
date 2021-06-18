#ifndef AFINA_COROUTINE_ENGINE_H
#define AFINA_COROUTINE_ENGINE_H

#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <tuple>

#include <setjmp.h>

namespace Afina {
namespace Coroutine {

/**
 * # Entry point of coroutine library
 * Allows to run coroutine and schedule its execution. Not threadsafe
 */
class Engine final {
public:
    using unblocker_func = std::function<void(Engine &)>;

private:
    /**
     * A single coroutine instance which could be scheduled for execution
     * should be allocated on heap
     */
    struct context;
    typedef struct context {
        // coroutine stack start address
        char *Low = nullptr;

        // coroutine stack end address
        char *Hight = nullptr;

        // coroutine stack copy buffer
        std::tuple<char *, uint32_t> Stack = std::make_tuple(nullptr, 0);

        // Saved coroutine context (registers)
        jmp_buf Environment;

        // To include routine in the different lists, such as "alive", "blocked", e.t.c
        struct context *prev = nullptr;
        struct context *next = nullptr;
    } context;

    /**
     * Where coroutines stack begins
     */
    char *StackBottom;

    /**const int&
     * Current coroutine
     */
    context *cur_routine;

    /**
     * List of routines ready to be scheduled. Note that suspended routine ends up here as well
     */
    context *alive;

    /**
     * List of corountines that sleep and can't be executed
     */
    context *blocked;

    /**
     * Context to be returned finally
     */
    context *idle_ctx;

    /**
     * Call when all coroutines are blocked
     */
    unblocker_func _unblocker;

protected:
    /**
     * Save stack of the current coroutine in the given context
     */
    void Store(context &ctx);

    /**
     * Restore stack of the given context and pass control to coroutinne
     */
    void Restore(context &ctx);

    static void null_unblocker(Engine &) {}

public:
    Engine(unblocker_func unblocker = null_unblocker)
        : StackBottom(0), cur_routine(nullptr), alive(nullptr), _unblocker(unblocker) {}
    Engine(Engine &&) = delete;
    Engine(const Engine &) = delete;
    ~Engine();
    /**
     * Gives up current routine execution and let engine to schedule other one. It is not defined when
     * routine will get execution back, for example if there are no other coroutines then executing could
     * be trasferred back immediately (yield turns to be noop).
     *
     * Also there are no guarantee what coroutine will get execution, it could be caller of the current one or
     * any other which is ready to run
     */
    void yield();

    /**
     * Suspend current routine and transfers control to the given one, resumes its execution from the point
     * when it has been suspended previously.
     *
     * If routine to pass execution to is not specified (nullptr) then method should behaves like yield. In case
     * if passed routine is the current one method does nothing
     */
    void sched(void *routine);

    /**
     * Blocks current routine so that is can't be scheduled anymore
     * If it was a currently running corountine, then do yield to select new one to be run instead.
     *
     * If argument is nullptr then block current coroutine
     */
    void block(void *coro = nullptr);

    /**
     * Put coroutine back to list of alive, so that it could be scheduled later
     */
    void unblock(void *coro);

    /**
     * Entry point into the engine. Prepare all internal mechanics and starts given function which is
     * considered as main.
     *
     * Once control returns back to caller of start all coroutines are done execution, in other words,
     * this function doesn't return control until all coroutines are done.
     *
     * @param pointer to the main coroutine
     * @param arguments to be passed to the main coroutine
     */
    template <typename... Ta> void start(void (*main)(Ta...), Ta &&... args) {
        // To acquire stack begin, create variable on stack and remember its address
        char StackStartsHere;
        this->StackBottom = &StackStartsHere;

        // Start routine execution
        void *pc = run(main, std::forward<Ta>(args)...);

        idle_ctx = new context();
        idle_ctx->Low = StackBottom;
        idle_ctx->Hight = StackBottom;
        if (setjmp(idle_ctx->Environment) > 0) {
            if (alive == nullptr) {
                _unblocker(*this);
            }

            // Here: correct finish of the coroutine section
            yield();
        } else if (pc != nullptr) {
            Store(*idle_ctx);
            sched(pc);
        }

        // Shutdown runtime
        delete[] std::get<0>(idle_ctx->Stack);
        delete idle_ctx;
        this->StackBottom = 0;
    }

    /**
     * Register new coroutine. It won't receive control until scheduled explicitely or implicitly. In case of some
     * errors function returns -1
     */
    template <typename... Ta> void *run(void(*func)(Ta...), Ta &&... args){
        char addr;
        return _run(&addr, func, std::forward<Ta>(args)...);
    }

    template <typename... Ta> void *_run(char* addr, void (*func)(Ta...), Ta &&... args) {
        if (this->StackBottom == 0) {
            // Engine wasn't initialized yet
            return nullptr;
        }

        // New coroutine context that carries around all information enough to call function
        context *pc = new context();
        pc->Hight=pc->Low = addr;

        // Store current state right here, i.e just before enter new coroutine, later, once it gets scheduled
        // execution starts here. Note that we have to acquire stack of the current function call to ensure
        // that function parameters will be passed along
        if (setjmp(pc->Environment) > 0) {
            // Created routine got control in order to start execution. Note that all variables, such as
            // context pointer, arguments and a pointer to the function comes from restored stack

            // invoke routine
            func(std::forward<Ta>(args)...);

            // Routine has completed its execution, time to delete it. Note that we should be extremely careful in where
            // to pass control after that. We never want to go backward by stack as that would mean to go backward in
            // time. Function run() has already return once (when setjmp returns 0), so return second return from run
            // would looks a bit awkward
            if (pc->prev != nullptr) {
                pc->prev->next = pc->next;
            }

            if (pc->next != nullptr) {
                pc->next->prev = pc->prev;
            }

            if (alive == cur_routine) {
                alive = alive->next;
            }

            // current coroutine finished, and the pointer is not relevant now
            cur_routine = nullptr;
            pc->prev = pc->next = nullptr;
            delete std::get<0>(pc->Stack);
            delete pc;

            // We cannot return here, as this function "returned" once already, so here we must select some other
            // coroutine to run. As current coroutine is completed and can't be scheduled anymore, it is safe to
            // just give up and ask scheduler code to select someone else, control will never returns to this one
            Restore(*idle_ctx);
        }

        // setjmp remembers position from which routine could starts execution, but to make it correctly
        // it is neccessary to save arguments, pointer to body function, pointer to context, e.t.c - i.e
        // save stack.
        Store(*pc);

        // Add routine as alive double-linked list
        pc->next = alive;
        alive = pc;
        if (pc->next != nullptr) {
            pc->next->prev = pc;
        }

        return pc;
    }
};

} // namespace Coroutine
} // namespace Afina

#endif // AFINA_COROUTINE_ENGINE_H
