// Defer.h

#pragma once

#include <functional>
#include <utility>

class Defer {
public:
    // 构造函数接受一个可调用对象
    explicit Defer(std::function<void()> callback)
        : callback_(std::move(callback)) {
    }

    // 析构函数在对象销毁时执行回调
    ~Defer() {
        if (callback_) {
            callback_();
        }
    }

    // 禁止拷贝
    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;

    // 允许移动
    Defer(Defer&& other) noexcept
        : callback_(std::move(other.callback_)) {
        other.callback_ = nullptr;
    }

    Defer& operator=(Defer&& other) noexcept {
        if (this != &other) {
            callback_ = std::move(other.callback_);
            other.callback_ = nullptr;
        }
        return *this;
    }

private:
    std::function<void()> callback_;
};