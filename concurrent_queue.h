#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <Windows.h>

class Lock {
    friend class LockGuarde;
protected:
    Lock() : destination(0), compare(0), exchange(42) {}
    ~Lock() = default;
protected:
    const long compare;
    const long exchange;
    volatile long destination;
public:
    virtual void acquire() = 0;
    virtual void release() = 0;
};

class Mutex {
public:
    Mutex() : winMutex(NULL) {
        winMutex = CreateMutex(
            NULL,              // default security attributes
            FALSE,             // initially not owned
            NULL);
    }
    ~Mutex() {
        CloseHandle(winMutex);
    }

    void lock() {
        DWORD dwWaitResult;
        dwWaitResult = WaitForSingleObject(
            winMutex,    // handle to mutex
            INFINITE);  // no time-out interval
        if (dwWaitResult == WAIT_FAILED) throw std::runtime_error::exception("Mutex init exception");
    }
    void unlock() {
        if (!ReleaseMutex(winMutex))
        {
            throw std::runtime_error::exception("Mutex init exception");
        }
    }
private:
    HANDLE winMutex;
};

template<typename T, typename = std::enable_if_t<std::is_base_of_v<Mutex, T>>>
class UniqueLock : public Lock {
public:
    UniqueLock(T Mutex, bool lock = true) : mutex(Mutex) {
        if (lock)
            try {
                mutex.lock();
            }
            catch (std::runtime_error::exception const& e) {
            
            }
    }
    ~UniqueLock() { 
        try {
            mutex.unlock();
        } 
        catch (std::runtime_error::exception const& e) {

        }
    }
public:
    void acquire() override { 
        try {
            mutex.lock();
        }
        catch (std::runtime_error::exception const& e) {

        }
    }
    void release() override {
        try {
            mutex.unlock();
        }
        catch (std::runtime_error::exception const& e) {

        }
    }
protected:
    Mutex& mutex;
};

template<class F>
concept ParamneterFn = requires(F f)
{ { f() } -> std::same_as<void>;
};

class ConditionVariable {
public:
    template<ParamneterFn F>
    void wait(UniqueLock<Mutex>& LockObj, F func);
    void notify_one() {
        try {
            mutex.unlock();
        }
        catch (std::runtime_error::exception const& e) { exit(1); }
    }
protected:
    const long YIELD_ITERATION = 30;
    const long MAX_SLEEP_ITERATION = 40;
    long destination;
    Mutex mutex;
};

class SpinLock : public Lock {
protected:
    const long YIELD_ITERATION = 30;
    const long MAX_SLEEP_ITERATION = 40;
public:
    SpinLock() : YIELD_ITERATION(30), MAX_SLEEP_ITERATION(40) {}
    ~SpinLock() = default;
public:
    void acquire() override;
    void release() override { destination = 0; }
};

class LockGuarde {
public:
    LockGuarde(Lock& Lock) : LockObj(Lock)
    { LockObj.acquire(); }

    ~LockGuarde() { LockObj.release(); }
protected:
    Lock& LockObj;
};

template <typename T>
struct concurrent_queue {

    concurrent_queue() : max_size(100) {}

    bool try_push(T&& value) {
        std::unique_lock<std::mutex> lock(mutex);
        if (queue.size() == max_size)
            return false;
        queue.push_back(std::move(value));
        lock.unlock();
        cv_empty.notify_one();
        return true;
    }

    bool try_push(T const& value) {
        std::unique_lock<std::mutex> lock(mutex);
        if (queue.size() == max_size)
            return false;
        queue.push_back(value);
        lock.unlock();
        cv_empty.notify_one();
        return true;
    }

    void push(T&& value) {
        std::unique_lock<std::mutex> lock(mutex);
        cv_full.wait(lock, [&]
            {
                return queue.size() != max_size;
            });
        queue.push_back(std::move(value));
        lock.unlock();
        cv_empty.notify_one();
    }

    void push(T const& value) {
        std::unique_lock<std::mutex> lock(mutex);
        cv_full.wait(lock, [&]
            {
                return queue.size() != max_size;
            });
        queue.push_back(std::ref(value));
        lock.unlock();
        cv_empty.notify_one();
    }

    std::optional<T> try_pop()
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (queue.empty())
            return std::nullopt;
        T result = queue.front();
        queue.pop_front();
        lock.unlock();
        cv_full.notify_one();
        return result;
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex);
        cv_empty.wait(lock, [&]
            {
                return !queue.empty();
            });
        T result = queue.front();
        queue.pop_front();
        lock.unlock();
        cv_full.notify_one();
        return result;
    }

private:
    mutable std::mutex mutex;
    std::condition_variable cv_empty;
    std::condition_variable cv_full;
    std::deque<T> queue;
    size_t max_size = 100;
};