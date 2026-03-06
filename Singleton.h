// Singleton.h

#pragma once
template<typename T>
class Singleton {
public:
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    static T& getInstance() {
        static T instance;  // C++11 保证线程安全
        return instance;
    }

protected:
    Singleton() = default;
    virtual ~Singleton() = default;
};