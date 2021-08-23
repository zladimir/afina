#include <afina/coroutine/Engine.h>

#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <cstring>
#include <csetjmp>

namespace Afina {
namespace Coroutine {

void Engine::Store(context &ctx){
	char cur_stack;

    if(&cur_stack > StackBottom){
        ctx.Hight = &cur_stack;
    } else {
        ctx.Low = &cur_stack;
    }

    auto stack_size = ctx.Hight - ctx.Low;
    std::cerr<<stack_size<<std::endl;
    if (stack_size > std::get<1>(ctx.Stack) || std::get<1>(ctx.Stack) > stack_size * 2){
        delete[] std::get<0>(ctx.Stack);
        std::get<0>(ctx.Stack) = new char[stack_size];
        std::get<1>(ctx.Stack) = stack_size;
    }

    std::memcpy(std::get<0>(ctx.Stack), ctx.Low, stack_size);
}

void Engine::Restore(context &ctx){
	char cur_stack;
    while (ctx.Low <= &cur_stack && ctx.Hight >= &cur_stack)
    {
        Restore(ctx);
    }
    std::memcpy(ctx.Low, std::get<0>(ctx.Stack), ctx.Hight - ctx.Low);
    std::longjmp(ctx.Environment, 1);
}

void Engine::yield(){
    auto it = alive;
    if(it && it == cur_routine){
        it = it -> next;
    }
    if(it){
        sched(it);
    }
}

void Engine::sched(void *routine_){
	if(routine_ == cur_routine || routine_ == nullptr){
        return yield();
    }

    if(cur_routine){
        Store(*cur_routine);
        if(setjmp(cur_routine -> Environment)){
            return;
        }
    }
    cur_routine = (context *)routine_;
    Restore(*(context*)routine_);
}

Engine::~Engine(){
    for (auto coro = alive; coro != nullptr;){
        auto tmp = coro;
        coro = coro->next;
        delete[] std::get<0> (tmp->Stack);
        delete tmp;
    }
}

} // namespace Coroutine
} // namespace Afina
