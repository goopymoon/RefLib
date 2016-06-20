#pragma once

#include <stdint.h>
#include "reflib_type_def.h"

namespace RefLib
{

class CompositId
{
public:
    CompositId(uint32 id, uint32 salt=0)
        : _id(id)
        , _salt(salt)
    {
    }

    uint32 GetId() const { return _id; }
    uint64 GetCompId() const { return (_id << 16 | _salt); }

    // call when NetConnection is reused.
    void IncSalt()
    {
        _salt = (_salt + 1) % UINT32_MAX;
    }

private:
    uint32 _id;
    uint32 _salt;
};

} // namespace RefLib