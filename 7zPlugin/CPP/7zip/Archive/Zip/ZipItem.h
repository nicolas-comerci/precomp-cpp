// Archive/ZipItem.h

#ifndef __ARCHIVE_ZIP_ITEM_H
#define __ARCHIVE_ZIP_ITEM_H

#include "../../../../C/CpuArch.h"

#include "../../../Common/MyBuffer.h"
#include "../../../Common/MyString.h"
#include "../../../Common/StringConvert.h"
#include "../../../Common/UTFConvert.h"

#include "ZipHeader.h"

namespace NArchive {
namespace NZip {

struct CVersion
{
  Byte Version;
  Byte HostOS;
};

struct CExtraSubBlock
{
  UInt16 ID;
  CByteBuffer Data;

  bool ExtractNtfsTime(unsigned index, FILETIME &ft) const;
  bool ExtractUnixTime(bool isCentral, unsigned index, UInt32 &res) const;
};

const unsigned k_WzAesExtra_Size = 7;

struct CWzAesExtra
{
  UInt16 VendorVersion; // 1: AE-1, 2: AE-2,
  // UInt16 VendorId; // 'A' 'E'
  Byte Strength; // 1: 128-bit, 2: 192-bit, 3: 256-bit
  UInt16 Method;

  CWzAesExtra(): VendorVersion(2), Strength(3), Method(0) {}

  bool NeedCrc() const { return (VendorVersion == 1); }

  bool ParseFromSubBlock(const CExtraSubBlock &sb)
  {
    if (sb.ID != NFileHeader::NExtraID::kWzAES)
      return false;
    if (sb.Data.Size() < k_WzAesExtra_Size)
      return false;
    const Byte *p = (const Byte *)sb.Data;
    VendorVersion = GetUi16(p);
    if (p[2] != 'A' || p[3] != 'E')
      return false;
    Strength = p[4];
    // 9.31: The BUG was fixed:
    Method = GetUi16(p + 5);
    return true;
  }
  
  void SetSubBlock(CExtraSubBlock &sb) const
  {
    sb.Data.Alloc(k_WzAesExtra_Size);
    sb.ID = NFileHeader::NExtraID::kWzAES;
    Byte *p = (Byte *)sb.Data;
    p[0] = (Byte)VendorVersion;
    p[1] = (Byte)(VendorVersion >> 8);
    p[2] = 'A';
    p[3] = 'E';
    p[4] = Strength;
    p[5] = (Byte)Method;
    p[6] = (Byte)(Method >> 8);
  }
};

namespace NStrongCrypto_AlgId
{
  const UInt16 kDES = 0x6601;
  const UInt16 kRC2old = 0x6602;
  const UInt16 k3DES168 = 0x6603;
  const UInt16 k3DES112 = 0x6609;
  const UInt16 kAES128 = 0x660E;
  const UInt16 kAES192 = 0x660F;
  const UInt16 kAES256 = 0x6610;
  const UInt16 kRC2 = 0x6702;
  const UInt16 kBlowfish = 0x6720;
  const UInt16 kTwofish = 0x6721;
  const UInt16 kRC4 = 0x6801;
}

struct CStrongCryptoExtra
{
  UInt16 Format;
  UInt16 AlgId;
  UInt16 BitLen;
  UInt16 Flags;

  bool ParseFromSubBlock(const CExtraSubBlock &sb)
  {
    if (sb.ID != NFileHeader::NExtraID::kStrongEncrypt)
      return false;
    const Byte *p = (const Byte *)sb.Data;
    if (sb.Data.Size() < 8)
      return false;
    Format = GetUi16(p + 0);
    AlgId  = GetUi16(p + 2);
    BitLen = GetUi16(p + 4);
    Flags  = GetUi16(p + 6);
    return (Format == 2);
  }

  bool CertificateIsUsed() const { return (Flags > 0x0001); }
};

struct CExtraBlock
{
  CObjectVector<CExtraSubBlock> SubBlocks;
  
  void Clear() { SubBlocks.Clear(); }
  
  size_t GetSize() const
  {
    size_t res = 0;
    FOR_VECTOR (i, SubBlocks)
      res += SubBlocks[i].Data.Size() + 2 + 2;
    return res;
  }
  
  bool GetWzAes(CWzAesExtra &e) const
  {
    FOR_VECTOR (i, SubBlocks)
      if (e.ParseFromSubBlock(SubBlocks[i]))
        return true;
    return false;
  }

  bool HasWzAes() const
  {
    CWzAesExtra e;
    return GetWzAes(e);
  }

  bool GetStrongCrypto(CStrongCryptoExtra &e) const
  {
    FOR_VECTOR (i, SubBlocks)
      if (e.ParseFromSubBlock(SubBlocks[i]))
        return true;
    return false;
  }

  /*
  bool HasStrongCrypto() const
  {
    CStrongCryptoExtra e;
    return GetStrongCrypto(e);
  }
  */

  bool GetNtfsTime(unsigned index, FILETIME &ft) const
  {
    FOR_VECTOR (i, SubBlocks)
    {
      const CExtraSubBlock &sb = SubBlocks[i];
      if (sb.ID == NFileHeader::NExtraID::kNTFS)
        return sb.ExtractNtfsTime(index, ft);
    }
    return false;
  }

  bool GetUnixTime(bool isCentral, unsigned index, UInt32 &res) const
  {
    FOR_VECTOR (i, SubBlocks)
    {
      const CExtraSubBlock &sb = SubBlocks[i];
      if (sb.ID == NFileHeader::NExtraID::kUnixTime)
        return sb.ExtractUnixTime(isCentral, index, res);
    }
    return false;
  }

  void RemoveUnknownSubBlocks()
  {
    for (unsigned i = SubBlocks.Size(); i != 0;)
    {
      i--;
      if (SubBlocks[i].ID != NFileHeader::NExtraID::kWzAES)
        SubBlocks.Delete(i);
    }
  }
};


class CLocalItem
{
public:
  UInt16 Flags;
  UInt16 Method;
  CVersion ExtractVersion;

  UInt64 Size;
  UInt64 PackSize;
  UInt32 Time;
  UInt32 Crc;
  
  AString Name;

  CExtraBlock LocalExtra;

  bool IsUtf8() const { return (Flags & NFileHeader::NFlags::kUtf8) != 0; }
  bool IsEncrypted() const { return (Flags & NFileHeader::NFlags::kEncrypted) != 0; }
  bool IsStrongEncrypted() const { return IsEncrypted() && (Flags & NFileHeader::NFlags::kStrongEncrypted) != 0; }
  bool IsAesEncrypted() const { return IsEncrypted() && (IsStrongEncrypted() || Method == NFileHeader::NCompressionMethod::kWzAES); }
  bool IsLzmaEOS() const { return (Flags & NFileHeader::NFlags::kLzmaEOS) != 0; }
  bool HasDescriptor() const { return (Flags & NFileHeader::NFlags::kDescriptorUsedMask) != 0; }
  
  bool IsDir() const;

  /*
  void GetUnicodeString(const AString &s, UString &res) const
  {
    bool isUtf8 = IsUtf8();
    if (isUtf8)
      if (ConvertUTF8ToUnicode(s, res))
        return;
    MultiByteToUnicodeString2(res, s, GetCodePage());
  }
  */

private:

  void SetFlag(unsigned bitMask, bool enable)
  {
    if (enable)
      Flags |= bitMask;
    else
      Flags &= ~bitMask;
  }

public:

  void ClearFlags() { Flags = 0; }
  void SetEncrypted(bool encrypted) { SetFlag(NFileHeader::NFlags::kEncrypted, encrypted); }
  void SetUtf8(bool isUtf8) { SetFlag(NFileHeader::NFlags::kUtf8, isUtf8); }
  void SetDescriptorMode(bool useDescriptor) { SetFlag(NFileHeader::NFlags::kDescriptorUsedMask, useDescriptor); }

  UINT GetCodePage() const { return CP_OEMCP; }
};


class CItem: public CLocalItem
{
public:
  CVersion MadeByVersion;
  UInt16 InternalAttrib;
  UInt32 ExternalAttrib;
  
  UInt64 LocalHeaderPos;
  
  CExtraBlock CentralExtra;
  CByteBuffer Comment;

  bool FromLocal;
  bool FromCentral;
  
  // CItem can be used as CLocalItem. So we must clear unused fields
  CItem():
      InternalAttrib(0),
      ExternalAttrib(0),
      FromLocal(false),
      FromCentral(false)
  {
    MadeByVersion.Version = 0;
    MadeByVersion.HostOS = 0;
  }

  bool IsDir() const;
  UInt32 GetWinAttrib() const;
  bool GetPosixAttrib(UInt32 &attrib) const;

  Byte GetHostOS() const { return FromCentral ? MadeByVersion.HostOS : ExtractVersion.HostOS; }

  void GetUnicodeString(const AString &s, UString &res, bool useSpecifiedCodePage, UINT codePage) const
  {
    bool isUtf8 = IsUtf8();
    bool ignore_Utf8_Errors = true;

    #ifdef _WIN32
    if (!isUtf8)
    {
      if (useSpecifiedCodePage)
        isUtf8 = (codePage == CP_UTF8);
      else if (GetHostOS() == NFileHeader::NHostOS::kUnix)
      {
        /* Some ZIP archives in Unix use UTF-8 encoding without Utf8 flag in header.
           We try to get name as UTF-8.
           Do we need to do it in POSIX version also? */
        isUtf8 = true;
        ignore_Utf8_Errors = false;
      }
    }
    #endif

    if (isUtf8)
      if (ConvertUTF8ToUnicode(s, res) || ignore_Utf8_Errors)
        return;
    MultiByteToUnicodeString2(res, s, useSpecifiedCodePage ? codePage : GetCodePage());
  }

  bool IsThereCrc() const
  {
    if (Method == NFileHeader::NCompressionMethod::kWzAES)
    {
      CWzAesExtra aesField;
      if (CentralExtra.GetWzAes(aesField))
        return aesField.NeedCrc();
    }
    return (Crc != 0 || !IsDir());
  }
  
  UINT GetCodePage() const
  {
    Byte hostOS = GetHostOS();
    return (UINT)((
        hostOS == NFileHeader::NHostOS::kFAT ||
        hostOS == NFileHeader::NHostOS::kNTFS) ? CP_OEMCP : CP_ACP);
  }
};

}}

#endif
