#include <stdio.h>
#include <string.h>
#include "libprecomp.h"

FILE* open_input_file(char* in_file, bool allow_stdin) {
  FILE* fin = NULL;
  if (strcmp(in_file, "stdin") == 0) {
    if (!allow_stdin) {
      print_to_terminal("ERROR: stdin can't be used for precompression as precompression requires a seekable stream\n");
    }
    else {
      fin = stdin;
    }
  } else
  {
    fin = fopen(in_file, "rb");
    if (fin == NULL) {
      print_to_terminal("ERROR: Input file \"%s\" doesn't exist\n", in_file);
    }
  }

  return fin;
}

FILE* open_output_file(char* out_file) {
  FILE* fout = NULL;
  if (strcmp(out_file, "stdout") == 0) {
    fout = stdout;
  }
  else
  {
    fout = fopen(out_file, "a+b");
    if (fout == NULL) {
      print_to_terminal("ERROR: Output file \"%s\" doesn't exist\n", out_file);
    }
  }

  return fout;
}

// Generic IStream File functions
size_t read_from_file(void* backing_structure, char* buff, long long count) { return fread(buff, 1, count, backing_structure); }
int getc_from_file(void* backing_structure) { return fgetc(backing_structure); }
int seek_file(void* backing_structure, long long pos, int dir) { return fseek(backing_structure, pos, dir); }
long long tell_file(void* backing_structure) { ftell(backing_structure); }
bool eof_file(void* backing_structure) { feof(backing_structure); }
bool error_file(void* backing_structure) { ferror(backing_structure); }
void clear_file_error(void* backing_structure) { clearerr(backing_structure); }

bool set_precomp_input(CPrecomp* precomp_mgr, char* filename, bool use_generic_streams) {
  FILE* fin = open_input_file(filename, false);
  if (fin == NULL) { return false; }
  if (use_generic_streams) {
    PrecompSetGenericInputStream(precomp_mgr, filename, fin, &read_from_file, &getc_from_file, &seek_file, &tell_file, &eof_file, &error_file, &clear_file_error);
  }
  else {
    PrecompSetInputFile(precomp_mgr, fin, filename);
  }
  return true;
}

// Generic OStream File functions
size_t write_to_file(void* backing_structure, char const* buff, long long count) { return fwrite(buff, 1, count, backing_structure); }
int putc_to_file(void* backing_structure, int chr) { return fputc(chr, backing_structure); }

bool set_precomp_output(CPrecomp* precomp_mgr, char* filename, bool use_generic_streams) {
  FILE* fout = open_output_file(filename);
  if (fout == NULL) { return false; }
  if (use_generic_streams) {
    PrecompSetGenericOutputStream(precomp_mgr, filename, fout, &write_to_file, &putc_to_file, &seek_file, &tell_file, &eof_file, &error_file, &clear_file_error);
  }
  else {
    PrecompSetOutputFile(precomp_mgr, fout, filename);
  }
  return true;
}

int precompress_file(char* in_file, char* out_file, bool use_generic_streams) {
  CPrecomp* precomp_mgr = PrecompCreate();
  int filesize_err = 0;
  CRecursionContext* context = PrecompGetRecursionContext(precomp_mgr);
  context->fin_length = fileSize64(in_file, &filesize_err);

  bool input_success = set_precomp_input(precomp_mgr, in_file, use_generic_streams);
  if (!input_success) return 1;

  bool output_success = set_precomp_output(precomp_mgr, out_file, use_generic_streams);
  if (!output_success) return 1;

  int result = PrecompPrecompress(precomp_mgr);
  PrecompDestroy(precomp_mgr);
  return result;
}

int recompress_file(char* in_file, char* out_file, bool use_generic_streams) {
  CPrecomp* precomp_mgr = PrecompCreate();
  int filesize_err = 0;
  PrecompGetRecursionContext(precomp_mgr)->fin_length = fileSize64(in_file, &filesize_err);

  bool input_success = set_precomp_input(precomp_mgr, in_file, use_generic_streams);
  if (!input_success) return 1;

  bool output_success = set_precomp_output(precomp_mgr, out_file, use_generic_streams);
  if (!output_success) return 1;

  int result = PrecompRecompress(precomp_mgr);
  PrecompDestroy(precomp_mgr);
  return result;
}

const int precomp_arg_count = 3;
const int recomp_arg_count = 4;

typedef enum { PRECOMPRESS, RECOMPRESS } precomp_op;

const char* syntax_msg = "\nSyntax: dlltest [p|r][g|] input_file output_file\n";

int main(int argc, char* argv[]) {
  char msg[256];

  if (argc != 4) {
    print_to_terminal(syntax_msg);
    return -1;
  }
  char* input_file_name = argv[2];
  char* output_file_name = argv[3];

  precomp_op operation = argv[1][0] == 'p' ? PRECOMPRESS : argv[1][0] == 'r' ? RECOMPRESS : -1;
  if (operation == -1 || strlen(argv[1]) > 2) {
    print_to_terminal(syntax_msg);
    return -1;
  }

  // In generic streams mode, we give Precomp a FILE* that it just sees as a void*, and the appropriate functions to read/write etc from it
  // Precomp has absolutely no idea where the data is coming from or where it's going, and we have complete control over the process, instead of a FILE* it could be anything.
  // This is to demo/exemplify Precomp's flexibility in that it can be made to take input from or dump output to pretty much ANYWHERE... with a little elbow grease
  bool use_generic_streams = false;
  if (strlen(argv[1]) == 2) {
    if (argv[1][1] != 'g') {
      print_to_terminal(syntax_msg);
      return -1;
    }
    use_generic_streams = true;
  }

  print_to_terminal("\nCopyright message:\n");
  PrecompGetCopyrightMsg(msg);
  print_to_terminal("%s\n", msg);

  int precomp_ret_code;
  if (operation == PRECOMPRESS) {
    print_to_terminal("\nPrecompressing %s into %s:\n", input_file_name, output_file_name);
    precomp_ret_code = precompress_file(input_file_name, output_file_name, use_generic_streams);
    if (precomp_ret_code == 0) {
      print_to_terminal("File %s was precompressed successfully to %s.\n", input_file_name, output_file_name);
    }
  } else {
    print_to_terminal("\nRecompressing %s into %s:\n", input_file_name, output_file_name);
    precomp_ret_code = recompress_file(input_file_name, output_file_name, use_generic_streams);
    if (precomp_ret_code == 0) {
      print_to_terminal("File %s was recompressed successfully to %s.\n", input_file_name, output_file_name);
    }
  }
  if (precomp_ret_code != 0) {
    print_to_terminal("ERROR: Precomp returned error code %i.\n", precomp_ret_code);
  }

  return precomp_ret_code;
}
