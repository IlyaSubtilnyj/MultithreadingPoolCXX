#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include "concurrent_queue.h"
#include <condition_variable>
#include <iterator>
#include <fstream>
#include <iostream>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <thread>
#include <optional>
#include <windows.h>


enum class TASK_TYPE : int32_t { EXECUTE, TERMINATE, THROW, CHECK, LOG, CLEAR };

struct TaskType {
public:
    TaskType(concurrent_queue<std::shared_ptr<TaskType>>* queue, enum class TASK_TYPE task_type = TASK_TYPE::EXECUTE) : complited_tasks_queue(queue), type(task_type) {}
protected:
    enum class TASK_TYPE type;
public:
    int32_t execute() { if (type == TASK_TYPE::TERMINATE) return 0; else return 1; }
    DWORD virtual one_thread_method(LPVOID) = 0;
    struct concurrent_queue<std::shared_ptr<TaskType>>* getQueue() { return complited_tasks_queue; }
protected:
    struct concurrent_queue<std::shared_ptr<TaskType>>* complited_tasks_queue;
};

struct TerminateTask : public TaskType {
public:
    TerminateTask(concurrent_queue<std::shared_ptr<TaskType>>* queue = NULL) : TaskType(queue, TASK_TYPE::TERMINATE) {}
    DWORD one_thread_method(LPVOID) override { return 0; }
};

typedef struct MyData : public TaskType {
public:
    MyData(std::vector<std::string>&& array, concurrent_queue<std::shared_ptr<TaskType>>* queue) : TaskType(queue), array(std::move(array)) {}
protected:
    std::vector<std::string> array;
public:
    std::vector<std::string>& getArray() { return this->array; }
public:
    DWORD one_thread_method(LPVOID lpvThreadParam) override {
        std::sort(array.begin(), array.end(), std::less<std::string>());
        return 0;
    }
} MYDATA, * PMYDATA;

/**
    @struct TaskManager
    @brief  Manager of task receiving from client threads and execution using its own thread pool.
    @tparam routine           - threads function start address
    @tparam routine_parameter - arguments passed to routine function
**/

struct TaskManager {
public:
    TaskManager(int32_t pool_size) : pool_size(pool_size) {
        //for (int32_t i = 0; i < pool_size; i++)
        //    pool.push_back(std::thread(&TaskManager::thread_routine, this));
        for (int32_t i = 0; i < pool_size; i++) {
            DWORD identifier;
            hThreadArray[i] = CreateThread(NULL, 0, TaskManager::thread_routine, this, 0, &identifier);
        }
    }
    ~TaskManager()
    {
        TerminateTask term_task = TerminateTask(NULL);
        for (size_t i = 0; i < pool_size; i++)
            routine_parameters_queue.push(std::make_shared<TerminateTask>(term_task));
        //for (std::thread& thread : pool)
        //    thread.join();
        WaitForMultipleObjects(pool_size, hThreadArray, TRUE, INFINITE);
    }

    template<typename task_t, typename = std::enable_if_t<std::is_base_of_v<TaskType, task_t>>>
    bool try_add_task(task_t const& task) { return routine_parameters_queue.try_push(std::make_shared<task_t>(task)); }

    template<typename task_t, typename = std::enable_if_t<std::is_base_of_v<TaskType, task_t>>>
    bool add_task(task_t const& task) { return routine_parameters_queue.push(std::make_shared<task_t>(task)); }

public:
    static DWORD WINAPI thread_routine(LPVOID lpThreadParameter) {
        TaskManager* manager = static_cast<TaskManager*>(lpThreadParameter);
        DWORD err{};
        while (true) 
        {
            std::shared_ptr<TaskType> operating_task(manager->routine_parameters_queue.pop());
            if (!(err = operating_task->execute())) 
                break;
            operating_task->one_thread_method(NULL);
            operating_task->getQueue()->push(operating_task);
        } 
        return err;
    }
protected:
    std::vector<std::thread> pool;
    int32_t pool_size;
    struct concurrent_queue<std::shared_ptr<TaskType>> routine_parameters_queue;
    HANDLE hThreadArray[5];
};

class Consumer {
public:
    Consumer(std::shared_ptr<TaskManager> taskManager) : manager(taskManager) {}
protected:
    std::shared_ptr<TaskManager> manager;
    struct concurrent_queue<std::shared_ptr<TaskType>> complitedTasksQueue;
};

class CustomConsumer : public Consumer {
public:
    explicit CustomConsumer(std::shared_ptr<TaskManager> taskManager, std::string&& iFileName, std::string&& oFileName)
        : Consumer(taskManager), INPUT_FILE_NAME(std::move(iFileName)), OUTPUT_FILE_NAME(std::move(oFileName)), fileSplitedInStrings(0) {}
public:
    void run(size_t threads_count) {
        size_t _strings_in_file_cnt = getAllStringsInFile();
        std::vector<std::vector<std::string>> splitedVector;
        try {
            splitedVector = sliceVector(fileSplitedInStrings, threads_count);
        }
        catch (const std::runtime_error& e) {
            std::cout << e.what() << std::endl;
        }
        std::vector<MyData> tasks;
        for (size_t i = 0; i < threads_count; i++)
        {
            tasks.emplace_back(MyData(std::move(splitedVector.at(i)), &complitedTasksQueue));
        }
        for (size_t i = 0; i < threads_count; i++)
        {
            manager->try_add_task(tasks.at(i));
        }
        std::vector<MyData> res;
        for (size_t i = 0; i < threads_count; i++)
        {
            res.push_back(*static_cast<MyData*>(complitedTasksQueue.pop().get()));
        }
        std::vector<std::string> merge_result;
        merge_result.reserve(_strings_in_file_cnt);

        std::vector<std::vector<std::string>> sortedPartsVector;
        for (size_t i = 0; i < threads_count; i++)
        {
            sortedPartsVector.emplace_back(std::move(res.at(i).getArray()));
        }
        merge_result = merge(sortedPartsVector).front();
        dumpSortedStringsToFile(std::ref(merge_result));
    }
protected: //!< Custom functions
    size_t getAllStringsInFile() {
        std::ifstream rfile;
        rfile.exceptions(std::ifstream::badbit);
        size_t countStringsInFile{};
        try {
            rfile.open(INPUT_FILE_NAME);
            if (rfile.is_open()) {
                std::string fstring_buffer;
                while (std::getline(rfile, fstring_buffer)) {
                    fileSplitedInStrings.emplace_back(fstring_buffer);
                    countStringsInFile++;
                }
            }
        }
        catch (const std::ifstream::failure& e)
        {
            std::cerr << "error" << e.what() << std::endl;
            return -1;
        }
        return countStringsInFile;
    }
    std::vector<std::vector<std::string>> sliceVector(const std::vector<std::string>& input, int n) {
        if (input.size() < n) {
            throw std::runtime_error("The input vector is too small to be sliced into n parts.");
        }

        int size_per_part = input.size() / n;
        int remaining_size = input.size() % n;

        std::vector<std::vector<std::string>> result(n);

        int end_index = 0;
        int start_index = 0;
        for (int i = 0; i < n; ++i) {
            start_index = end_index;
            end_index = start_index + size_per_part;

            if (i < remaining_size) {
                end_index += 1; // Include the remaining elements if there are any
            }

            result[i] = std::vector<std::string>(input.begin() + start_index, input.begin() + end_index);
        }

        return result;
    }
    std::vector<std::vector<std::string>> merge(std::vector<std::vector<std::string>> tasks) {
        if (tasks.size() < 2) {
            return tasks;
        }

        int size_per_part = tasks.size() / 2;
        int remaining_size = tasks.size() % 2;

        if (remaining_size) {
            std::vector<std::string> extra_result;
            std::merge(tasks.at(tasks.size() - 2).begin(), tasks.at(tasks.size() - 2).end(), tasks.at(tasks.size() - 1).begin(), tasks.at(tasks.size() - 1).end(), std::back_inserter(extra_result));
            tasks.at(tasks.size() - 2) = std::move(extra_result);
        }

        std::vector<std::vector<std::string>> result(size_per_part);

        for (int32_t i = 0; i < size_per_part; i++)
        {
            std::merge(tasks.at(i * 2).begin(), tasks.at(i * 2).end(),
                tasks.at(i * 2 + 1).begin(), tasks.at(i * 2 + 1).end(), std::back_inserter(result.at(i)));
        }

        result = merge(result);

        return result;
    }
    void dumpSortedStringsToFile(std::vector<std::string>& vector) {
        std::ofstream output_file(OUTPUT_FILE_NAME);
        std::ostream_iterator<std::string> output_iterator(output_file, "\n");
        std::copy(std::begin(vector), std::end(vector), output_iterator);
    }
protected: //!< Custom variables
    const std::string INPUT_FILE_NAME;
    const std::string OUTPUT_FILE_NAME;
    std::vector<std::string> fileSplitedInStrings;
};

static size_t threads_count;

int main(int argc, char** argv) {
    (void*)argc, (void*)argv;
    //if (argc < 2) return;
    //threads_count = std::stoll(argv[1]);
    threads_count = 5;

    std::shared_ptr<TaskManager> sharedTaskManager = std::make_shared<TaskManager>(threads_count);
    CustomConsumer consumer = CustomConsumer(sharedTaskManager, "text.txt", "sorted_text.txt");
    consumer.run(threads_count);   
}

#if 0
CRITICAL_SECTION CriticalSection;

class A {
public:
    std::deque<int> queue1;
    std::deque<int> queue2;

    int32_t critical_section = 0;
    int32_t critical_section2 = 0;
    void add_1() {
        // Request ownership of the critical section.
        EnterCriticalSection(&CriticalSection);
        /*int temp = critical_section;
        Sleep(1);
        temp = temp + 1;
        critical_section = temp;
        Sleep(1);
        temp = 4;
        critical_section += temp;*/
        for (size_t i = 0; i < 100000; i++)
        {
            queue1.push_back(10000);
        }
        // Release ownership of the critical section.
        LeaveCriticalSection(&CriticalSection);
    }
    void add_2() {
        LockGuarde lock(std::ref(sl));
        /*int temp = critical_section2;
        Sleep(1);
        temp = temp + 1;
        critical_section2 = temp;
        Sleep(1);
        temp = 4;
        critical_section2 += temp;*/
        for (size_t i = 0; i < 100000; i++)
        {
            queue2.push_back(10000);
        }
    }
private:
    std::mutex m;
    SpinLock sl;
};

void test() {
    std::array<std::thread, 40> array;
    A a;
    auto begin = std::chrono::steady_clock::now();
    for (size_t i = 0; i < 40; i++)
    {
        array.at(i) = std::thread(&A::add_1, &a);
    }
    for (size_t i = 0; i < 40; i++)
    {
        array.at(i).join();
    }
    auto end = std::chrono::steady_clock::now();
    std::cout << a.critical_section << std::endl;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
    std::cout << "The time(Critical section): " << elapsed_ms.count() << " ms\n";
}

void test2() {
    std::array<std::thread, 40> array;
    A a;
    auto begin = std::chrono::steady_clock::now();
    for (size_t i = 0; i < 40; i++)
    {
        array.at(i) = std::thread(&A::add_2, &a);
    }
    for (size_t i = 0; i < 40; i++)
    {
        array.at(i).join();
    }
    auto end = std::chrono::steady_clock::now();
    std::cout << a.critical_section2 << std::endl;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
    std::cout << "The time(custom LockGuarde): " << elapsed_ms.count() << " ms\n";
}
#endif