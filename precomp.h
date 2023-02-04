#include "precomp_dll.h"

int def(std::istream& source, std::ostream& dest, int level, int windowbits, int memlevel);
long long def_compare(std::istream& compfile, int level, int windowbits, int memlevel, long long & decompressed_bytes_used, long long decompressed_bytes_total, bool in_memory);
int def_part(std::istream& source, std::ostream& dest, int level, int windowbits, int memlevel, long long stream_size_in, long long stream_size_out);
int def_part_skip(std::istream& source, std::ostream& dest, int level, int windowbits, int memlevel, long long stream_size_in, long long stream_size_out, int bmp_width);
void zerr(int ret);
#ifndef PRECOMPDLL
#ifndef COMFORT
int init(int argc, char* argv[]);
#else
int init_comfort(int argc, char* argv[]);
#endif
#endif
void denit_compress(std::string tmp_filename);
void denit_decompress(std::string tmp_filename);
bool intense_mode_is_active();
bool brute_mode_is_active();
int inf_bzip2(std::istream& source, std::ostream& dest, long long& compressed_stream_size, long long& decompressed_stream_size);
int def_bzip2(std::istream& source, std::ostream& dest, int level);
long long file_recompress(std::istream& origfile, int compression_level, int windowbits, int memlevel, long long& decompressed_bytes_used, long long decomp_bytes_total, bool in_memory);
void write_decompressed_data(long long byte_count, const char* decompressed_file_name);
void write_decompressed_data_io_buf(long long byte_count, bool in_memory, const char* decompressed_file_name);
unsigned long long compare_files(std::istream& file1, std::istream& file2, unsigned int pos1, unsigned int pos2);
long long compare_file_mem_penalty(std::istream& file1, unsigned char* input_bytes2, long long pos1, long long bytecount, long long& total_same_byte_count, long long& total_same_byte_count_penalty, long long& rek_same_byte_count, long long& rek_same_byte_count_penalty, long long& rek_penalty_bytes_len, long long& local_penalty_bytes_len, bool& use_penalty_bytes);
long long compare_files_penalty(std::istream& file1, std::istream& file2, long long pos1, long long pos2);
void start_uncompressed_data();
void end_uncompressed_data();
void try_decompression_pdf(int windowbits, int pdf_header_length, int img_width, int img_height, int img_bpc, PrecompTmpFile& tmpfile);
void try_decompression_zip(int zip_header_length, PrecompTmpFile& tmpfile);
void try_decompression_gzip(int gzip_header_length, PrecompTmpFile& tmpfile);
void try_decompression_png(int windowbits, PrecompTmpFile& tmpfile);
void try_decompression_gif(unsigned char version[5], PrecompTmpFile& tmpfile);
void try_decompression_jpg(long long jpg_length, bool progressive_jpg, PrecompTmpFile& tmpfile);
void try_decompression_mp3(long long mp3_length, PrecompTmpFile& tmpfile);
void try_decompression_zlib(int windowbits, PrecompTmpFile& tmpfile);
void try_decompression_brute(PrecompTmpFile& tmpfile);
void try_decompression_swf(int windowbits, PrecompTmpFile& tmpfile);
void try_decompression_bzip2(int compression_level, PrecompTmpFile& tmpfile);
void try_decompression_base64(int gzip_header_length, PrecompTmpFile& tmpfile);

// helpers for try_decompression functions

void init_decompression_variables();

void packjpg_mp3_dll_msg();
bool is_valid_mp3_frame(unsigned char* frame_data, unsigned char header2, unsigned char header3, int protection);
inline unsigned short mp3_calc_layer3_crc(unsigned char header2, unsigned char header3, unsigned char* sideinfo, int sidesize);
void sort_comp_mem_levels();
void show_used_levels();
bool compress_file(float min_percent = 0, float max_percent = 100);
void decompress_file();
void convert_file();
long long try_to_decompress(std::istream& file, int windowbits, long long& compressed_stream_size, bool& in_memory);
long long try_to_decompress_bzip2(std::istream& file, int compression_level, long long& compressed_stream_size, PrecompTmpFile& tmpfile);
void try_recompress(std::istream& origfile, int comp_level, int mem_level, int windowbits, long long& compressed_stream_size, long long decomp_bytes_total, bool in_memory);
void write_header();
void read_header();
void convert_header();
bool file_exists(const char* filename);
#ifdef COMFORT
  bool check_for_pcf_file();
#endif
std::fstream& tryOpen(const char* filename, std::ios_base::openmode mode);
long long fileSize64(const char* filename);
void print64(long long i64);
std::string temp_files_tag();
void printf_time(long long t);
void print_debug_percent();
void ctrl_c_handler(int sig);

class zLibMTF{
  struct MTFItem{
    int Next, Previous;
  };
  alignas(16) MTFItem List[81];
  int Root, Index;
public:
  zLibMTF(): Root(0), Index(0) {
    for (int i=0;i<81;i++){
      List[i].Next = i+1;
      List[i].Previous = i-1;
    }
    List[80].Next = -1;
  }
  inline int First(){
    return Index=Root;
  }
  inline int Next(){
    return (Index>=0)?Index=List[Index].Next:Index;
  }
  inline void Update(){
    if (Index==Root) return;
    
    List[ List[Index].Previous ].Next = List[Index].Next;
    if (List[Index].Next>=0)
      List[ List[Index].Next ].Previous = List[Index].Previous;
    List[Root].Previous = Index;
    List[Index].Next = Root;
    List[Root=Index].Previous = -1;
  }
};
struct recompress_deflate_result;

void write_ftempout_if_not_present(long long byte_count, bool in_memory, PrecompTmpFile& tmpfile);

int32_t fin_fget32_little_endian();
int32_t fin_fget32();
long long fin_fget_vlint();
void fin_fget_deflate_hdr(recompress_deflate_result&, const unsigned char flags, unsigned char* hdr_data, unsigned& hdr_length, const bool inc_last);
void fin_fget_recon_data(recompress_deflate_result&);
bool fin_fget_deflate_rec(recompress_deflate_result&, const unsigned char flags, unsigned char* hdr, unsigned& hdr_length, const bool inc_last, int64_t& rec_length, PrecompTmpFile& tmpfile);
void fin_fget_uncompressed(const recompress_deflate_result&);
void fout_fput32_little_endian(int v);
void fout_fput32(int v);
void fout_fput32(unsigned int v);
void fout_fput_vlint(unsigned long long v);
void fout_fput_deflate_hdr(const unsigned char type, const unsigned char flags, const recompress_deflate_result&, const unsigned char* hdr_data, const unsigned hdr_length, const bool inc_last);
void fout_fput_recon_data(const recompress_deflate_result&);
void fout_fput_uncompressed(const recompress_deflate_result&, PrecompTmpFile& tmpfile);

void fast_copy(std::istream& file1, std::ostream& file2, long long bytecount, bool update_progress = false);

unsigned char base64_char_decode(unsigned char c);
void base64_reencode(std::istream& file_in, std::ostream& file_out, int line_count, unsigned int* base64_line_len, long long max_in_count = 0x7FFFFFFFFFFFFFFF, long long max_byte_count = 0x7FFFFFFFFFFFFFFF);

bool recompress_gif(std::istream& srcfile, std::ostream& dstfile, unsigned char block_size, GifCodeStruct* g, GifDiffStruct* gd);
bool decompress_gif(std::istream& srcfile, std::ostream& dstfile, long long src_pos, int& gif_length, int& decomp_length, unsigned char& block_size, GifCodeStruct* g);

struct recursion_result {
  bool success;
  std::string file_name;
  long long file_length;
  std::shared_ptr<std::ifstream> frecurse = std::shared_ptr<std::ifstream>(new std::ifstream());
};
recursion_result recursion_compress(long long compressed_bytes, long long decompressed_bytes, PrecompTmpFile& tmpfile, bool deflate_type = false, bool in_memory = true);
recursion_result recursion_decompress(long long recursion_data_length, PrecompTmpFile& tmpfile);
recursion_result recursion_write_file_and_compress(const recompress_deflate_result&, PrecompTmpFile& tmpfile);
void fout_fput_deflate_rec(const unsigned char type, const recompress_deflate_result&, const unsigned char* hdr, const unsigned hdr_length, const bool inc_last, const recursion_result& recres, PrecompTmpFile& tmpfile);

void try_decompression_png_multi(std::istream& fpng, int windowbits, PrecompTmpFile& tmpfile);
