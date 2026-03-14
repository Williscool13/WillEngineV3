//
// Created by William on 2026-03-14.
//

#ifndef WILL_ENGINE_OVERLOADED_H
#define WILL_ENGINE_OVERLOADED_H

template<typename... Ts> struct overloaded : Ts... { using Ts::operator()...; };

#endif //WILL_ENGINE_OVERLOADED_H
