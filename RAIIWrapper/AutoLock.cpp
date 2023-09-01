struct Mutex {
    void Init()
    {
        KeInitializeMutex(&_mutx, 0);
    }

    void Lock()
    {
        KeWaitForSingleObject(&_mutex, Executive, KernelMode, FALSE, nullptr);
    }

    void Unlock()
    {
        KeReleaseMutex(&_mutex, FALSE);
    }

    private:
        KMUTEX _mutex;
}

template<typename TLock>
struct AutoLock{
    AutoLock(TLock& lock): _lock(lock){
        lock.Lock();
    }

    ~AutoLock(){
        _lock.Unlock();
    }

    private:
        TLock& _lock;
}


/*
SAMPLE CODE:


Mutex MyMutex;

void Init(){
    MyMutex.Init();
}

void DoWork()
{
    AutoLock<Mutex> locker(MyMutex);
}

*/