// PrecompRegister.cpp
// Portions of this module are from 7-zip, by Igor Pavlov, which you can download here:
// http://www.7-zip.org/

#include "../C/Alloc.h"

//#include "../Common/RegisterCodec.h"
#include "../CPP/Common/Common.h"
#include "../CPP/Common/MyCom.h"
#include "../CPP/7zip/ICoder.h"
#include "../CPP/7zip/Common/StreamUtils.h"
#include "../CPP/7zip/Common/RegisterCodec.h"

#include "libprecomp.h"
#include <precomp_utils.h>

#define PRECOMP_PROPS_VER (Byte)(0x1)

UInt64 dumpInStreamToFile(ISequentialInStream* inStream, FILE* outfile) {
    UInt32 inPos = 0;
    UInt32 inSize = 0;
    Byte buffer[512];
    UInt64 totalInSize = 0;

    for (;;)
    {
        if (inPos == inSize)
        {
            inPos = inSize = 0;
            inStream->Read(buffer, 512, &inSize);
            if (!inSize) break;
        }

        fwrite(buffer, 1, inSize, outfile);
        totalInSize += inSize;
        inSize = 0;
    }
    return totalInSize;
}

namespace NCompress
{
   namespace NPrecomp
   {
      class StreamWrapper {
      public:
          UInt64 streamSize;
          UInt64 pos;
          bool eof;
      protected:
          StreamWrapper(UInt64 _StreamSize) : streamSize(_StreamSize), pos(0), eof(false) {}
      };

      // Precomp Generic OStream ISequentialInStream implementation stuff
      class InStreamWrapper : public StreamWrapper {
      public:
          ISequentialInStream* inStream;
          UInt32 inBufferAllocatedSize;

          InStreamWrapper(ISequentialInStream* _inStream, UInt64 _inStreamSize, UInt32 _inBufferAllocatedSize) :
              inStream(_inStream), StreamWrapper(_inStreamSize), inBufferAllocatedSize(_inBufferAllocatedSize) {}
      };

      size_t read_from_seqinstream(void* backing_structure, char* buff, long long count) {
          auto inStreamWrapper = static_cast<InStreamWrapper*>(backing_structure);
          size_t readSizeAcum = 0;
          while (readSizeAcum < count) {
            UInt32 readSize = 0;
            inStreamWrapper->inStream->Read(buff, count - readSizeAcum, &readSize);
            if (readSize == 0) break;
            inStreamWrapper->pos += readSize;
            buff += readSize;
            readSizeAcum += readSize;
          }
          return readSizeAcum;
      }
      int getc_from_seqinstream(void* backing_structure) {
          auto inStreamWrapper = static_cast<InStreamWrapper*>(backing_structure);
          UInt32 readSize = 0;
          unsigned char buf[1];
          inStreamWrapper->inStream->Read(buf, 1, &readSize);
          inStreamWrapper->pos += readSize;
          if (readSize == 0 || buf[0] == EOF) {
            inStreamWrapper->eof = true;
          }
          return readSize == 1 ? buf[0] : EOF;
      }

      // Precomp Generic OStream ISequentialOutStream implementation stuff
      class OutStreamWrapper: public StreamWrapper {
      public:
          ISequentialOutStream* outStream;

          OutStreamWrapper(ISequentialOutStream* _outStream, UInt64 _outStreamSize):
              outStream(_outStream), StreamWrapper(_outStreamSize) {}
      };

      size_t write_to_seqoutstream(void* outStream, char const* buff, long long count) {
          auto outStreamWrapper = static_cast<OutStreamWrapper*>(outStream);
          auto result = WriteStream(outStreamWrapper->outStream, buff, count);
          outStreamWrapper->pos += count;
          return result != 0 ? 0 : count;
      }
      int putc_to_seqoutstream(void* outStream, int chr) {
          char buf[1];
          buf[0] = chr;
          auto amt_written = write_to_seqoutstream(outStream, buf, 1);
          return amt_written == 1 ? chr : EOF;
      }

      // Precomp Generic IStream/OStream shared implementation functions

      // This one is okay because we don't seek on outStreams, and we only use the inStream directly on recompression
      // and we don't need seeking on recompression so we just return a non implementation
      int seek_seqstream(void* backing_structure, long long pos, int dir) { return 0; }
      long long tell_seqstream(void* backing_structure) { return static_cast<StreamWrapper*>(backing_structure)->pos; }
      bool eof_seqstream(void* backing_structure) {
          auto streamWrapper = static_cast<StreamWrapper*>(backing_structure);
          return streamWrapper->eof;
      }
      bool error_seqstream(void* backing_structure) { return false; }
      void clear_seqstream_error(void* backing_structure) {}

      struct CProps
      {
         CProps() { clear(); }

         void clear() 
		 { 
			 memset(this, 0, sizeof(*this)); 
			 _ver = PRECOMP_PROPS_VER; 
			 _dict_size = 0; 
			 _level = 0; 
			 _flags = 0; 
		 }

         Byte _ver;
         Byte _dict_size;
         Byte _level;
         Byte _flags;
         Byte _reserved[1];
      };

      class CDecoder:
         public ICompressCoder,
         public ICompressSetDecoderProperties2,
         public ICompressSetBufSize,
#ifndef NO_READ_FROM_CODER
         public ICompressSetInStream,
         public ICompressSetOutStreamSize,
         public ISequentialInStream,
#endif
         public CMyUnknownImp
      {
         CMyComPtr<ISequentialInStream> _inStream;
         Byte *_inBuf;
         Byte *_outBuf;
         UInt32 _inPos;
         UInt32 _inSize;
         UInt64 inStreamSize;

         CProps _props;
         bool _propsWereSet;
         
         bool _outSizeDefined;
         UInt64 _outSize;
         UInt64 _inSizeProcessed;
         UInt64 _outSizeProcessed;

         UInt32 _inBufSizeAllocated;
         UInt32 _outBufSizeAllocated;
         UInt32 _inBufSize;
         UInt32 _outBufSize;

         HRESULT CreateBuffers();
         HRESULT CodeSpec(ISequentialInStream *inStream, ISequentialOutStream *outStream, ICompressProgressInfo *progress);
         
         HRESULT SetOutStreamSizeResume(const UInt64 *outSize);
         HRESULT CreateDecompressor();

      public:
         MY_QUERYINTERFACE_BEGIN2(ICompressCoder)
            MY_QUERYINTERFACE_ENTRY(ICompressSetDecoderProperties2)
            MY_QUERYINTERFACE_ENTRY(ICompressSetBufSize)
#ifndef NO_READ_FROM_CODER
            MY_QUERYINTERFACE_ENTRY(ICompressSetInStream)
            MY_QUERYINTERFACE_ENTRY(ICompressSetOutStreamSize)
            MY_QUERYINTERFACE_ENTRY(ISequentialInStream)
#endif
            MY_QUERYINTERFACE_END
            MY_ADDREF_RELEASE

            STDMETHOD(Code)(ISequentialInStream *inStream, ISequentialOutStream *outStream,
            const UInt64 *inSize, const UInt64 *outSize, ICompressProgressInfo *progress);
         STDMETHOD(SetDecoderProperties2)(const Byte *data, UInt32 size);
         STDMETHOD(SetOutStreamSize)(const UInt64 *outSize);
         STDMETHOD(SetInBufSize)(UInt32 streamIndex, UInt32 size);
         STDMETHOD(SetOutBufSize)(UInt32 streamIndex, UInt32 size);

#ifndef NO_READ_FROM_CODER

         STDMETHOD(SetInStream)(ISequentialInStream *inStream);
         STDMETHOD(ReleaseInStream)();
         STDMETHOD(Read)(void *data, UInt32 size, UInt32 *processedSize);

         HRESULT CodeResume(ISequentialOutStream *outStream, const UInt64 *outSize, ICompressProgressInfo *progress);
         UInt64 GetInputProcessedSize() const { return _inSizeProcessed; }

#endif
         
         CDecoder();
         virtual ~CDecoder();
      };

      CDecoder::CDecoder(): _inBuf(0), _outBuf(0), _propsWereSet(false), _outSizeDefined(false),
         _inBufSize(1 << 22),
         _outBufSize(1 << 22),
         _inBufSizeAllocated(0),
         _outBufSizeAllocated(0),
         _inSizeProcessed(0),
         _outSizeProcessed(0)
      {
         _inSizeProcessed = 0;
         _inPos = _inSize = 0;
      }

      CDecoder::~CDecoder()
      {
         //if (_precomp) PrecompDestroy(_precomp);
         MyFree(_inBuf);
         MyFree(_outBuf);
      }

      STDMETHODIMP CDecoder::SetInBufSize(UInt32 , UInt32 size) 
      { 
         _inBufSize = size; 
         return S_OK; 
      }

      STDMETHODIMP CDecoder::SetOutBufSize(UInt32 , UInt32 size) 
      { 
         _outBufSize = size; 
         return S_OK; 
      }

      HRESULT CDecoder::CreateBuffers()
      {
         if (_inBuf == 0 || _inBufSize != _inBufSizeAllocated)
         {
            MyFree(_inBuf);
            _inBuf = (Byte *)MyAlloc(_inBufSize);
            if (_inBuf == 0)
               return E_OUTOFMEMORY;
            _inBufSizeAllocated = _inBufSize;
         }

         if (_outBuf == 0 || _outBufSize != _outBufSizeAllocated)
         {
            MyFree(_outBuf);
            _outBuf = (Byte *)MyAlloc(_outBufSize);
            if (_outBuf == 0)
               return E_OUTOFMEMORY;
            _outBufSizeAllocated = _outBufSize;
         }

         return S_OK;
      }

      STDMETHODIMP CDecoder::SetDecoderProperties2(const Byte *prop, UInt32 size)
      {
         CProps *pProps = (CProps*)prop;

         if (size != sizeof(CProps))
            return E_FAIL;

         if (pProps->_ver != PRECOMP_PROPS_VER)
            return E_FAIL;

         memcpy(&_props, pProps, sizeof(CProps));
                  
         _propsWereSet = true;
         
         return CreateBuffers();
      }

      HRESULT CDecoder::CreateDecompressor()
      {
         if (!_propsWereSet)
            return E_FAIL;

         return S_OK;
      }

      HRESULT CDecoder::SetOutStreamSizeResume(const UInt64 *outSize)
      {
         _outSizeDefined = (outSize != NULL);
         if (_outSizeDefined)
            _outSize = *outSize;
         _outSizeProcessed = 0;

         RINOK(CreateDecompressor())

         return S_OK;
      }

      STDMETHODIMP CDecoder::SetOutStreamSize(const UInt64 *outSize)
      {
         _inSizeProcessed = 0;
         _inPos = _inSize = 0;
         RINOK(SetOutStreamSizeResume(outSize));
         return S_OK;
      }

      HRESULT CDecoder::CodeSpec(ISequentialInStream *inStream, ISequentialOutStream *outStream, ICompressProgressInfo *progress)
      {
         if (_inBuf == 0 || !_propsWereSet)
            return S_FALSE;

         UInt64 startInProgress = _inSizeProcessed;

         Precomp* precomp = PrecompCreate();

         InStreamWrapper inStreamWrapper{ inStream, inStreamSize, 0 };
         PrecompSetGenericInputStream(
           precomp, "whocares", &inStreamWrapper,
           &read_from_seqinstream, &getc_from_seqinstream,
           &seek_seqstream, &tell_seqstream,
           &eof_seqstream, &error_seqstream, &clear_seqstream_error
         );

         OutStreamWrapper outStreamWrapper{ outStream, _outSize };
         PrecompSetGenericOutputStream(
             precomp, "whocares", &outStreamWrapper,
             &write_to_seqoutstream, &putc_to_seqoutstream,
             &seek_seqstream, &tell_seqstream,
             &eof_seqstream, &error_seqstream, &clear_seqstream_error
         );

         int result = PrecompRecompress(precomp);
         PrecompDestroy(precomp);

         /*
         for (;;)
         {
            bool eofFlag = false;
            if (_inPos == _inSize)
            {
               _inPos = _inSize = 0;
               RINOK(inStream->Read(_inBuf, _inBufSizeAllocated, &_inSize));
               if (!_inSize)
                  eofFlag = true;
            }

            lzham_uint8 *pIn_bytes = _inBuf + _inPos;
            size_t num_in_bytes = _inSize - _inPos;
            lzham_uint8* pOut_bytes = _outBuf;
            size_t out_num_bytes = _outBufSize;
            if (_outSizeDefined)
            {
               UInt64 out_remaining = _outSize - _outSizeProcessed;
               if (out_num_bytes > out_remaining)
                  out_num_bytes = static_cast<size_t>(out_remaining);
            }
            
            lzham_decompress_status_t status = lzham_decompress(_state, pIn_bytes, &num_in_bytes, pOut_bytes, &out_num_bytes, eofFlag);
            
            if (num_in_bytes)
            {
               _inPos += (UInt32)num_in_bytes;
               _inSizeProcessed += (UInt32)num_in_bytes;
            }
                        
            if (out_num_bytes)
            {
               _outSizeProcessed += out_num_bytes;

               RINOK(WriteStream(outStream, _outBuf, out_num_bytes));
            }
            
            if (status >= LZHAM_DECOMP_STATUS_FIRST_FAILURE_CODE)
               return S_FALSE;

            if (status == LZHAM_DECOMP_STATUS_SUCCESS)
               break;
                        
            UInt64 inSize = _inSizeProcessed - startInProgress;
            if (progress)
            {
               RINOK(progress->SetRatioInfo(&inSize, &_outSizeProcessed));
            }
         }
         */

         return result == 0 ? S_OK : S_FALSE;
      }

      STDMETHODIMP CDecoder::Code(ISequentialInStream *inStream, ISequentialOutStream *outStream,
         const UInt64 * inSize, const UInt64 *outSize, ICompressProgressInfo *progress)
      {
         if (_inBuf == 0)
            return E_INVALIDARG;
         SetOutStreamSize(outSize);
         inStreamSize = *inSize;
         return CodeSpec(inStream, outStream, progress);
      }
      
#ifndef NO_READ_FROM_CODER
      STDMETHODIMP CDecoder::SetInStream(ISequentialInStream *inStream) 
      { 
         _inStream = inStream; 
         return S_OK; 
      }

      STDMETHODIMP CDecoder::ReleaseInStream() 
      { 
         _inStream.Release(); 
         return S_OK; 
      }

      STDMETHODIMP CDecoder::Read(void *data, UInt32 size, UInt32 *processedSize)
      {
         if (_inBuf == 0 || !_propsWereSet)
            return S_FALSE;

         if (processedSize)
            *processedSize = 0;

         /*
         while (size != 0)
         {
            bool eofFlag = false;
            if (_inPos == _inSize)
            {
               _inPos = _inSize = 0;
               RINOK(_inStream->Read(_inBuf, _inBufSizeAllocated, &_inSize));
               if (!_inSize)
                  eofFlag = true;
            }

            lzham_uint8 *pIn_bytes = _inBuf + _inPos;
            size_t num_in_bytes = _inSize - _inPos;
            lzham_uint8* pOut_bytes = (lzham_uint8*)data;
            size_t out_num_bytes = size;

            lzham_decompress_status_t status = lzham_decompress(_state, pIn_bytes, &num_in_bytes, pOut_bytes, &out_num_bytes, eofFlag);

            if (num_in_bytes)
            {
               _inPos += (UInt32)num_in_bytes;
               _inSizeProcessed += num_in_bytes;
            }

            if (out_num_bytes)
            {
               _outSizeProcessed += out_num_bytes;
               size -= (UInt32)out_num_bytes;
               if (processedSize)
                  *processedSize += (UInt32)out_num_bytes;
            }

            if (status >= LZHAM_DECOMP_STATUS_FIRST_FAILURE_CODE)
               return S_FALSE;

            if (status == LZHAM_DECOMP_STATUS_SUCCESS)
               break;
         }
         */

         return S_OK;
      }

      HRESULT CDecoder::CodeResume(ISequentialOutStream *outStream, const UInt64 *outSize, ICompressProgressInfo *progress)
      {
         RINOK(SetOutStreamSizeResume(outSize));
         return CodeSpec(_inStream, outStream, progress);
      }
#endif

   } // namespace NPrecomp
} // namespace NCompress

static void *CreateCodec() 
{ 
   return (void *)(ICompressCoder *)(new NCompress::NPrecomp::CDecoder); 
}

#ifndef EXTRACT_ONLY

namespace NCompress
{
   namespace NPrecomp
   {
      class CEncoder:
         public ICompressCoder,
         public ICompressSetCoderProperties,
         public ICompressWriteCoderProperties,
         public CMyUnknownImp
      {
         CProps _props;
         bool _dictSizeSet;
         int _num_threads;
         
         Byte *_inBuf;
         Byte *_outBuf;
         UInt32 _inPos;
         UInt32 _inSize;

         UInt32 _inBufSizeAllocated;
         UInt32 _outBufSizeAllocated;
         UInt32 _inBufSize;
         UInt32 _outBufSize;

         UInt64 _inSizeProcessed;
         UInt64 _outSizeProcessed;
         
         HRESULT CreateCompressor();
         HRESULT CreateBuffers();

      public:
         MY_UNKNOWN_IMP2(ICompressSetCoderProperties, ICompressWriteCoderProperties)

         STDMETHOD(Code)(ISequentialInStream *inStream, ISequentialOutStream *outStream,
            const UInt64 *inSize, const UInt64 *outSize, ICompressProgressInfo *progress);

         STDMETHOD(SetCoderProperties)(const PROPID *propIDs, const PROPVARIANT *props, UInt32 numProps);

         STDMETHOD(WriteCoderProperties)(ISequentialOutStream *outStream);

         CEncoder();
         virtual ~CEncoder();
      };

      CEncoder::CEncoder() :
         _dictSizeSet(false),
         _num_threads(-1),
         _inBuf(NULL),
         _outBuf(NULL),
         _inPos(0),
         _inSize(0),
         _inBufSizeAllocated(0),
         _outBufSizeAllocated(0),
         _inBufSize(1 << 22),
         _outBufSize(1 << 22),
         _inSizeProcessed(0),
         _outSizeProcessed(0)
      {
      }

      CEncoder::~CEncoder()
      {
         MyFree(_inBuf);
         MyFree(_outBuf);
      }

      STDMETHODIMP CEncoder::SetCoderProperties(const PROPID *propIDs,
         const PROPVARIANT *coderProps, UInt32 numProps)
      {
         _props.clear();
         
         for (UInt32 i = 0; i < numProps; i++)
         {
            const PROPVARIANT &prop = coderProps[i];
            PROPID propID = propIDs[i];
            switch (propID)
            {
               //case NCoderPropID::kEndMarker:
               //   if (prop.vt != VT_BOOL) return E_INVALIDARG; props.writeEndMark = (prop.boolVal == VARIANT_TRUE); break;
               case NCoderPropID::kAlgorithm:
               {
                  if (prop.vt != VT_UI4)
                     return E_INVALIDARG;

                  bool val = (UInt32)prop.ulVal != 0;

#if LZHAMCODEC_DEBUG_OUTPUT                  
                  printf("Algorithm: %u\n", prop.ulVal);
#endif

                  break;
               }
               case NCoderPropID::kNumThreads:
               {
                  if (prop.vt != VT_UI4) 
                     return E_INVALIDARG; 
                  _num_threads = prop.ulVal; 

#if LZHAMCODEC_DEBUG_OUTPUT                  
                  printf("Num threads: %u\n", _num_threads);
#endif
                  break;
               }
               case NCoderPropID::kDictionarySize:
               {
                  if (prop.vt != VT_UI4)
                     return E_INVALIDARG;
                  /*
                  lzham_uint32 bits = 15;
                  while ((1U << bits) < prop.ulVal)
                     bits++;
#ifdef _WIN64
                  if (bits > LZHAM_MAX_DICT_SIZE_LOG2_X64)
#else
                  if (bits > LZHAM_MAX_DICT_SIZE_LOG2_X86)
#endif
                  {
                     return E_INVALIDARG;
                  }

                  _props._dict_size = bits; 
                  
#if LZHAMCODEC_DEBUG_OUTPUT                  
                  printf("Dict size: %u\n", bits);
#endif
                  */

                  break;
               }
               case NCoderPropID::kLevel: 
               {
                  if (prop.vt != VT_UI4)
                     return E_INVALIDARG;
                                                         
                  switch (prop.ulVal)
                  {
                     case 0:
                        _props._level = 0; if (!_props._dict_size) _props._dict_size = 18;
                        break;
                     case 1:
                        _props._level = 0; if (!_props._dict_size) _props._dict_size = 20;
                        break;
                     case 2:
                        _props._level = 1; if (!_props._dict_size) _props._dict_size = 21;
                        break;
                     case 3:
                        _props._level = 2; if (!_props._dict_size) _props._dict_size = 21;
                        break;
                     case 4:
                        _props._level = 2; if (!_props._dict_size) _props._dict_size = 22;
                        break;
                     case 5:
                        _props._level = 3; if (!_props._dict_size) _props._dict_size = 22;
                        break;
                     case 6:
                        _props._level = 3; if (!_props._dict_size) _props._dict_size = 23;
                        break;
                     case 7:
                        _props._level = 4; if (!_props._dict_size) _props._dict_size = 25;
                        break;
                     case 8:
                        _props._level = 4; if (!_props._dict_size) _props._dict_size = 26;
                        break;
                     case 9:
                        _props._level = 4; if (!_props._dict_size) _props._dict_size = 26;
                        //_props._flags |= LZHAM_COMP_FLAG_EXTREME_PARSING;
                        break;
                     default: 
                        return E_INVALIDARG;
                  }

#if LZHAMCODEC_DEBUG_OUTPUT                                                      
                  printf("Level: %u\n", prop.ulVal);
#endif
                  break;
               }
               default:
               {
                  //RINOK(SetLzmaProp(propID, prop, props));
                  break;
               }
            }
         }

         return S_OK;
      }

      STDMETHODIMP CEncoder::WriteCoderProperties(ISequentialOutStream *outStream)
      {
         return WriteStream(outStream, &_props, sizeof(_props));
      }

      HRESULT CEncoder::CreateBuffers()
      {
         if (_inBuf == 0 || _inBufSize != _inBufSizeAllocated)
         {
            MyFree(_inBuf);
            _inBuf = (Byte *)MyAlloc(_inBufSize);
            if (_inBuf == 0)
               return E_OUTOFMEMORY;
            _inBufSizeAllocated = _inBufSize;
         }

         if (_outBuf == 0 || _outBufSize != _outBufSizeAllocated)
         {
            MyFree(_outBuf);
            _outBuf = (Byte *)MyAlloc(_outBufSize);
            if (_outBuf == 0)
               return E_OUTOFMEMORY;
            _outBufSizeAllocated = _outBufSize;
         }

         return S_OK;
      }

      HRESULT CEncoder::CreateCompressor()
      {
         /*
         SYSTEM_INFO system_info;
         GetSystemInfo(&system_info);

         if (_num_threads < 0)
         {
            if (system_info.dwNumberOfProcessors > 1)
               params.m_max_helper_threads = system_info.dwNumberOfProcessors - 1;
         }
         else if (_num_threads > 1)
         {
            params.m_max_helper_threads = _num_threads - 1;
         }

         if (system_info.dwNumberOfProcessors > 1)
         {
            if (params.m_max_helper_threads > ((int)system_info.dwNumberOfProcessors - 1))
               params.m_max_helper_threads = system_info.dwNumberOfProcessors - 1;
         }

         if (params.m_max_helper_threads > LZHAM_MAX_HELPER_THREADS)
            params.m_max_helper_threads = LZHAM_MAX_HELPER_THREADS;

         params.m_dict_size_log2 = _props._dict_size ? _props._dict_size : 26;

         if (params.m_dict_size_log2 < LZHAM_MIN_DICT_SIZE_LOG2)
            params.m_dict_size_log2 = LZHAM_MIN_DICT_SIZE_LOG2;
         else 
         {
#ifdef _WIN64
            if (params.m_dict_size_log2 > LZHAM_MAX_DICT_SIZE_LOG2_X64)
               params.m_dict_size_log2 = LZHAM_MAX_DICT_SIZE_LOG2_X64;
#else
            if (params.m_dict_size_log2 > LZHAM_MAX_DICT_SIZE_LOG2_X86)
               params.m_dict_size_log2 = LZHAM_MAX_DICT_SIZE_LOG2_X86;
#endif
         }

         params.m_compress_flags = (lzham_compress_flags)_props._flags;

         params.m_level = (lzham_compress_level)_props._level;

#if LZHAMCODEC_DEBUG_OUTPUT
         printf("lzham_compress_params:\nmax_helper_threads: %u, dict_size_log2: %u, level: %u, deterministic_parsing: %u, extreme_parsing: %u\n", 
            params.m_max_helper_threads, params.m_dict_size_log2, params.m_level, (_props._flags & LZHAM_COMP_FLAG_DETERMINISTIC_PARSING) != 0, (_props._flags & LZHAM_COMP_FLAG_EXTREME_PARSING) != 0);
#endif

         _state = lzham_compress_init(&params);
         if (!_state)
            return S_FALSE;
         */

         return S_OK;
      }

      STDMETHODIMP CEncoder::Code(ISequentialInStream *inStream, ISequentialOutStream *outStream,
         const UInt64* inStreamSize, const UInt64* outStreamSize, ICompressProgressInfo *progress)
      {
         RINOK(CreateCompressor())
         
         RINOK(CreateBuffers())

         UInt64 startInProgress = _inSizeProcessed;
         UInt64 startOutProgress = _outSizeProcessed;

         auto filename = "C:\\" + temp_files_tag() + "_7zPrecomp";
         FILE* ftmp = fopen(filename.c_str(), "a+b");

         // Copy Input to temp file
         UInt64 totalInSize = dumpInStreamToFile(inStream, ftmp);
         /*
         for (;;)
         {
            lzham_uint8 *pIn_bytes = _inBuf + _inPos;
            size_t num_in_bytes = _inSize - _inPos;
            lzham_uint8* pOut_bytes = _outBuf;
            size_t out_num_bytes = _outBufSize;
            
            lzham_compress_status_t status = lzham_compress(_state, pIn_bytes, &num_in_bytes, pOut_bytes, &out_num_bytes, eofFlag);

            if (num_in_bytes)
            {
               _inPos += (UInt32)num_in_bytes;
               _inSizeProcessed += (UInt32)num_in_bytes;
            }

            if (out_num_bytes)
            {
               _outSizeProcessed += out_num_bytes;

               RINOK(WriteStream(outStream, _outBuf, out_num_bytes));
            }

            if (status >= LZHAM_DECOMP_STATUS_FIRST_FAILURE_CODE)
               return S_FALSE;

            if (status == LZHAM_DECOMP_STATUS_SUCCESS)
               break;

            UInt64 inSize = _inSizeProcessed - startInProgress;
            UInt64 outSize = _outSizeProcessed - startOutProgress;
            if (progress)
            {
               RINOK(progress->SetRatioInfo(&inSize, &outSize));
            }
         }
         */

         // Reopen file as read only and set it as input for precomp
         fclose(ftmp);
         int filesize_err = 0;
         Precomp* precomp = PrecompCreate();
         CRecursionContext* context = PrecompGetRecursionContext(precomp);
         context->fin_length = fileSize64(filename.c_str(), &filesize_err);
         ftmp = fopen(filename.c_str(), "rb");
         std::string stream_name { "7zPlugin_input" };
         PrecompSetInputFile(precomp, ftmp, stream_name.c_str());

         // Set output for precomp using the outStream
         OutStreamWrapper outStreamWrapper{ outStream, totalInSize };
         PrecompSetGenericOutputStream(
             precomp, "whocares", &outStreamWrapper,
             &write_to_seqoutstream, &putc_to_seqoutstream,
             &seek_seqstream, &tell_seqstream,
             &eof_seqstream, &error_seqstream, &clear_seqstream_error
         );

         // Finally execute Precomp
         int result = PrecompPrecompress(precomp);

         PrecompDestroy(precomp);
         fclose(ftmp);
         remove(filename.c_str());

         return result == 0 || result == 2 ? S_OK : S_FALSE;
      }
   }
}

static void *CreateCodecOut() 
{ 
   return static_cast<ICompressCoder*>(new NCompress::NPrecomp::CEncoder);  
}
#else
#define CreateCodecOut 0
#endif

static CCodecInfo g_CodecsInfo[1] =
{ 
   CreateCodec, 
   CreateCodecOut, 
   0x4F99001, 
   "PRECOMP", 
   1, 
   true 
};

REGISTER_CODECS(PRECOMP)
