//fastmutex.h

class FastMutex {
public:
    void Init();
    void Lock();
    void Unlock();

private:
    FAST_MUTEX _mutex;

};

//fastmutex.cpp
//#include "FastMutex.h"

void FastMutex::Init(){
    ExInitializeFastMutex(&_mutex);
}

void FastMutex::Lock()
{
    ExAquireFastMutex(&_mutex);
}

void FastMutex::Unlock()
{
    ExReleaseFastMutex(&_mutex);
}