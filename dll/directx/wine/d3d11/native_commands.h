/*
 * PROJECT: ReactOS Direct3D 11 runtime
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Command storage for native D3D11 deferred contexts
 */

template<class T>
class NativeCommandRef
{
    T *object;
public:
    explicit NativeCommandRef(T *value = NULL) : object(value) { if (object) object->AddRef(); }
    NativeCommandRef(const NativeCommandRef &other) : object(other.object) { if (object) object->AddRef(); }
    ~NativeCommandRef() { if (object) object->Release(); }
    T *Get() const { return object; }
private:
    NativeCommandRef &operator=(const NativeCommandRef &);
};

template<class T> static void NativeCommandRetain(const T &) {}
template<class T> static void NativeCommandDrop(const T &) {}
template<class T> static void NativeCommandRetain(T *const &object) { if (object) object->AddRef(); }
template<class T> static void NativeCommandDrop(T *const &object) { if (object) object->Release(); }

/* Copies API arrays and retains every interface in an interface-pointer array.
 * Copies of the wrapper share immutable storage, never application pointers. */
template<class T>
class NativeCommandArray
{
    struct Storage
    {
        LONG references;
        SIZE_T count;
        T values[1];
    };
    Storage *storage = NULL;
    bool valid = true;
    void Drop()
    {
        if (!storage || InterlockedDecrement(&storage->references)) return;
        for (SIZE_T i = 0; i < storage->count; ++i) NativeCommandDrop(storage->values[i]);
        HeapFree(GetProcessHeap(), 0, storage);
    }
public:
    NativeCommandArray() {}
    NativeCommandArray(SIZE_T count, const T *values)
    {
        if (!count) return;
        if (count > (~(SIZE_T)0 - offsetof(Storage, values)) / sizeof(T)) { valid = false; return; }
        storage = static_cast<Storage *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                offsetof(Storage, values) + count * sizeof(T)));
        if (!storage) { valid = false; return; }
        storage->references = 1;
        storage->count = count;
        if (values) memcpy(storage->values, values, count * sizeof(T));
        for (SIZE_T i = 0; i < count; ++i) NativeCommandRetain(storage->values[i]);
    }
    NativeCommandArray(const NativeCommandArray &other) : storage(other.storage), valid(other.valid)
    {
        if (storage) InterlockedIncrement(&storage->references);
    }
    NativeCommandArray &operator=(const NativeCommandArray &other)
    {
        if (other.storage) InterlockedIncrement(&other.storage->references);
        Drop();
        storage = other.storage;
        valid = other.valid;
        return *this;
    }
    ~NativeCommandArray() { Drop(); }
    bool Valid() const { return valid; }
    T *Data() const { return storage ? storage->values : NULL; }
    SIZE_T Count() const { return storage ? storage->count : 0; }
};

class NativeRecordedCommand : public NativeAllocation
{
public:
    NativeRecordedCommand *next = NULL;
    virtual ~NativeRecordedCommand() {}
    virtual void Execute(NativeContext *context) const = 0;
    static void DeleteList(NativeRecordedCommand *head)
    {
        while (head)
        {
            NativeRecordedCommand *next = head->next;
            delete head;
            head = next;
        }
    }
};

template<class F>
class NativeRecordedCall final : public NativeRecordedCommand
{
    F call;
public:
    explicit NativeRecordedCall(const F &value) : call(value) {}
    void Execute(NativeContext *context) const override { call(context); }
};

struct NativeDeferredMapping : public NativeAllocation
{
    NativeDeferredMapping *next = NULL;
    NativeCommandRef<ID3D11Resource> resource;
    UINT subresource;
    UINT row_bytes, rows, depth;
    bool mapped = false;
    NativeCommandArray<BYTE> bytes;
    NativeDeferredMapping(ID3D11Resource *value, UINT sub) : resource(value), subresource(sub) {}
};
