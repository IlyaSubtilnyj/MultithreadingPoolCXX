#include "concurrent_queue.h"

class Helper {
private:
    Helper() = default;
    ~Helper() = default;
private:
    // Helper function to count set bits in the processor mask.
    static DWORD CountSetBits(ULONG_PTR bitMask)
    {
        DWORD LSHIFT = sizeof(ULONG_PTR) * 8 - 1;
        DWORD bitSetCount = 0;
        ULONG_PTR bitTest = (ULONG_PTR)1 << LSHIFT;
        DWORD i;

        for (i = 0; i <= LSHIFT; ++i)
        {
            bitSetCount += ((bitMask & bitTest) ? 1 : 0);
            bitTest /= 2;
        }

        return bitSetCount;
    }
public:
    static int32_t GetNumberOfProcessors() {
        if (logicalProcessorCount == -1)
        {
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer;
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
            DWORD returnLength = 0;
            GetLogicalProcessorInformation(&buffer, &returnLength);
            DWORD byteOffset = 0;
            ptr = &buffer;
            DWORD numaNodeCount = 0;
            DWORD processorCoreCount = 0;
            DWORD logicalProcessorCount = 0;
            while (byteOffset + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= returnLength)
            {
                switch (ptr->Relationship)
                {
                case RelationNumaNode:
                    // Non-NUMA systems report a single record of this type.
                    numaNodeCount++;
                    break;

                case RelationProcessorCore:
                    processorCoreCount++;

                    // A hyperthreaded core supplies more than one logical processor.
                    logicalProcessorCount += CountSetBits(ptr->ProcessorMask);
                    break;
                }
                byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
                ptr++;
            }
        }
        else return logicalProcessorCount;
    }
protected:
    static DWORD numaNodeCount;
    static DWORD processorCoreCount;
    static DWORD logicalProcessorCount;
};
DWORD Helper::numaNodeCount = -1;
DWORD Helper::processorCoreCount = -1;
DWORD Helper::logicalProcessorCount = -1;


void SpinLock::acquire() {
    while (true)
    {
        if (InterlockedCompareExchange(&destination, exchange, compare) == 0)
        {
            //lock acquired
            break;
        }
        //lock not aquired
        int32_t m_iterations{};
        while (destination != compare)
        {
            if (m_iterations + YIELD_ITERATION >= MAX_SLEEP_ITERATION)
                Sleep(0);
            if (m_iterations >= YIELD_ITERATION && m_iterations < MAX_SLEEP_ITERATION)
                SwitchToThread();

            // Yield processor on multi-processor but if on single processor
            // then give other thread the CPU 
            m_iterations++;
            if (Helper::GetNumberOfProcessors() > 1)
            {
                YieldProcessor();
            }
            else { SwitchToThread(); }
        }
    }
}

template<ParamneterFn F>
void ConditionVariable::wait(UniqueLock<Mutex>& LockObj, F func) {
    while (!func())
    {
        if (InterlockedCompareExchange(&destination, 42, 0) == 0)
        {
            LockObj.release();
            try {
                mutex.lock();
            }
            catch (std::runtime_error::exception const& e) { exit(1); }
            LockObj.acquire();
            break;
        }
        int32_t m_iterations{};
        while (destination != 0)
        {
            if (m_iterations + YIELD_ITERATION >= MAX_SLEEP_ITERATION)
                Sleep(0);
            if (m_iterations >= YIELD_ITERATION && m_iterations < MAX_SLEEP_ITERATION)
                SwitchToThread();

            // Yield processor on multi-processor but if on single processor
            // then give other thread the CPU 
            m_iterations++;
            if (Helper::GetNumberOfProcessors() > 1)
            {
                YieldProcessor();
            }
            else { SwitchToThread(); }
        }
    }
    destination = 0;
}