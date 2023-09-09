template<typename T = void>
struct kunique_ptr {
    kunique_ptr(T* p = nullptr) : _p(p) {}

    //remove copy ctor and copy (single owner)
    kunique_ptr(const kunique_ptr&) = delete;
    kunique_ptr& operator=(const kunique_ptr&) = delete;


    //allow ownership transfer
    kunique_ptr(kunique_ptr&& other) : _p(other._p){
        other._p = nullptr;
    }

    kunique_ptr& operator=(kunique_ptr&& other){
        if (&other != this)
        {
            Release();
            _p = other._p;
            other._p = nullptr;
        }
        return *this;
    }

    ~kunique_ptr() {
        Release();
    }

    operator bool() const {
        return _p != nullptr;
    }

    T* operator->() const {
        return _p;
    }

    T& operator*() const {
        return *_p; 
    }

    void Release() {
        if (_p)
            ExFreePool(_p);
    }

    private:
        T* _p;
};