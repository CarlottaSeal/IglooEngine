#pragma once
#include <cstdint>
#include <mutex>
#include <vector>

enum JobType: uint32_t
{
    JOB_TYPE_WORKER = 0x01,
    JOB_TYPE_IO = 0x02,
    JOB_TYPE_COUNT
};

class Job
{
public:
    Job(uint32_t jobType = JOB_TYPE_WORKER);
    virtual ~Job() = default;
    
    virtual void Execute() = 0;

    virtual void OnComplete() = 0;
    
    void SetJobType(uint32_t type) { m_jobType = type; }

public:
    uint32_t m_jobType = 0;  
};

class JobSystem;

class JobWorkerThread
{
public:
    JobWorkerThread(int id, uint32_t type, JobSystem* system);
    ~JobWorkerThread();

    void Join();
    
private:
    bool CanExecuteJob(Job* job);
    void ThreadMain();
    
private:
    int m_threadID = 0;
    uint32_t m_workerType;
    JobSystem* m_jobSystem;
    std::thread* m_thread;
    bool m_isQuitting = false;
};

struct JobSystemConfig
{
    int m_numWorkerThreads;
    int m_numIOThreads;
};

class JobSystem
{
public:
    JobSystem(JobSystemConfig config);
    ~JobSystem();
    
    void Startup();
    void Shutdown();
    
    void AddPendingJob(Job* job);
    std::vector<Job*> RetrieveCompletedJobs();
    Job* RetrieveOneCompletedJob();

    void PrintDebugInfo();

    int GetPendingJobCount() const;
    int GetExecutingJobCount() const;
    int GetCompletedJobCount() const;
    int GetPendingAndExecutingJobCount() const;
    int GetNumWorkerThreads() const;

    void SetQuitting(bool isQuitting);
    bool IsQuitting() const;

private:
    friend class JobWorkerThread;

    int m_numWorkerThreads;
    int m_numIOThreads;
    std::vector<Job*> m_pendingJobs;      
    std::vector<Job*> m_executingJobs;    
    std::vector<Job*> m_completedJobs;    
    
    mutable std::mutex m_jobQueueMutex;
    std::condition_variable m_workAvailable;
    
    std::vector<JobWorkerThread*> m_workerThreads;
    std::atomic<bool> m_isQuitting{false};
};

extern JobSystem* g_theJobSystem;