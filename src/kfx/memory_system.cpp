#include "kfx/memory_system_c.h"

#include <string>
#include <unordered_map>

#include "kfx_memory.h"

namespace {

/**
 * Internal metadata tracked for a managed resource entry.
 */
struct ManagedResource {
    void** buffer_ptr;
    size_t capacity;
    KfxManagedDomain domain;
    unsigned int flags;
};

/**
 * Minimal ownership and capacity registry used by legacy C and new C++ code.
 *
 * The current implementation focuses on one contract:
 * callers can request a required byte capacity by resource id before writing.
 */
class MemorySystem {
public:
    void init()
    {
        if (initialized_) {
            return;
        }
        resources_.clear();
        initialized_ = true;
    }

    void shutdown()
    {
        resources_.clear();
        initialized_ = false;
    }

    int registerExternal(
        const char* resourceId,
        void** bufferPtr,
        size_t initialCapacity,
        KfxManagedDomain domain,
        unsigned int flags)
    {
        if (!initialized_) {
            init();
        }
        if (!resourceId || resourceId[0] == '\0' || !bufferPtr) {
            return 0;
        }

        ManagedResource entry = {};
        entry.buffer_ptr = bufferPtr;
        entry.capacity = initialCapacity;
        entry.domain = domain;
        entry.flags = flags;
        resources_[resourceId] = entry;
        return 1;
    }

    int ensureCapacity(const char* resourceId, size_t requiredCapacity)
    {
        if (!initialized_ || !resourceId || requiredCapacity == 0) {
            return 0;
        }

        auto it = resources_.find(resourceId);
        if (it == resources_.end()) {
            return 0;
        }
        ManagedResource& res = it->second;
        if (requiredCapacity <= res.capacity && res.buffer_ptr && *res.buffer_ptr != nullptr) {
            return 1;
        }

        if (!res.buffer_ptr) {
            return 0;
        }

        if (*res.buffer_ptr == nullptr) {
            *res.buffer_ptr = KfxCalloc(1, requiredCapacity);
        } else {
            *res.buffer_ptr = KfxRealloc(*res.buffer_ptr, requiredCapacity);
        }

        if (*res.buffer_ptr == nullptr) {
            return 0;
        }

        res.capacity = requiredCapacity;
        return 1;
    }

    size_t getCapacity(const char* resourceId) const
    {
        if (!initialized_ || !resourceId) {
            return 0;
        }

        auto it = resources_.find(resourceId);
        if (it == resources_.end()) {
            return 0;
        }
        return it->second.capacity;
    }

    /**
     * Free all external buffers registered under the given domain.
     * Registry entries are kept so callers do not need to re-register;
     * tracked capacity is reset to zero so the next ensureCapacity
     * triggers a fresh allocation.
     * @return number of resources that were freed.
     */
    int releaseDomain(KfxManagedDomain domain)
    {
        if (!initialized_) {
            return 0;
        }

        int released = 0;
        for (auto& kv : resources_) {
            ManagedResource& res = kv.second;
            if (res.domain != domain) {
                continue;
            }
            if ((res.flags & KFX_MANAGED_EXTERNAL) && res.buffer_ptr && *res.buffer_ptr != nullptr) {
                KfxFree(*res.buffer_ptr);
                *res.buffer_ptr = nullptr;
            }
            res.capacity = 0;
            ++released;
        }
        return released;
    }

private:
    std::unordered_map<std::string, ManagedResource> resources_;
    bool initialized_ = false;
};

// As declared inside of anomymous namespace, this is internal linkage and not visible outside this translation unit.
// As declared inside of anonymous namespace, this is internal linkage and not visible outside this translation unit.
MemorySystem g_memorySystem;

} // namespace

extern "C" void kfx_memory_system_init(void)
{
    g_memorySystem.init();
}

extern "C" void kfx_memory_system_shutdown(void)
{
    g_memorySystem.shutdown();
}

extern "C" int kfx_memory_register_external_buffer(
    const char* resource_id,
    void** buffer_ptr,
    size_t initial_capacity,
    KfxManagedDomain domain,
    unsigned int flags)
{
    return g_memorySystem.registerExternal(resource_id, buffer_ptr, initial_capacity, domain, flags);
}

extern "C" int kfx_memory_ensure_capacity(const char* resource_id, size_t required_capacity)
{
    return g_memorySystem.ensureCapacity(resource_id, required_capacity);
}

extern "C" size_t kfx_memory_get_capacity(const char* resource_id)
{
    return g_memorySystem.getCapacity(resource_id);
}

extern "C" int kfx_memory_release_domain(KfxManagedDomain domain)
{
    return g_memorySystem.releaseDomain(domain);
}