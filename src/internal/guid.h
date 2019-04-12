#pragma once
#ifndef XGUID_H
#define XGUID_H

#include <string>
#include <stdio.h>
#ifdef WIN32
#include <objbase.h>
#pragma comment(lib, "Ole32.lib")
#define snprintf _snprintf
#else
#include <uuid/uuid.h>
typedef struct _GUID
{
  unsigned long Data1;
  unsigned short Data2;
  unsigned short Data3;
  unsigned char Data4[8];
} GUID, UUID;
#endif
namespace XGUID
{
  GUID CreateGuid()
  {
    GUID guid;
#ifdef WIN32
    CoCreateGuid(&guid);
#else
    uuid_generate(reinterpret_cast<unsigned char *>(&guid));
#endif
    return guid;
  }
  std::string GuidToString(const GUID &guid)
  {
    char buf[64] = { 0 };
    snprintf(
      buf,
      sizeof(buf),
      "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
      guid.Data1, guid.Data2, guid.Data3,
      guid.Data4[0], guid.Data4[1],
      guid.Data4[2], guid.Data4[3],
      guid.Data4[4], guid.Data4[5],
      guid.Data4[6], guid.Data4[7]);
    return std::string(buf);
  }
  std::string CreateGuidString()
  {
    return GuidToString(CreateGuid());
  }

}//namespace
#endif