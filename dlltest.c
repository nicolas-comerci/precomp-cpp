#include <stdio.h>
#include <string.h>
#include "libprecomp.h"

int precompress_file(char* in_file, char* out_file) {
  CPrecomp* precomp_mgr = PrecompCreate();
  int filesize_err = 0;
  PrecompGetRecursionContext(precomp_mgr)->fin_length = fileSize64(in_file, &filesize_err);

  FILE* fin = NULL;
  if (strcmp(in_file, "stdin") == 0)  {
    printf("ERROR: stdin can't be used for precompression as precompression requires a seekable stream");
    return 1;
  }
  fin = fopen(in_file, "rb");
  if (fin == NULL) {
    printf("ERROR: Input file \"%s\" doesn't exist", in_file);
    return 1;
  }

  FILE* fout = NULL;
  if (strcmp(in_file, "stdout") == 0) {
    fout = stdout;
  } else
  {
    fout = fopen(out_file, "a+b");
  }

  PrecompSetInputFile(precomp_mgr, fin, in_file);
  PrecompSetOutputFile(precomp_mgr, fout, out_file);
  return PrecompPrecompress(precomp_mgr);
}

int recompress_file(char* in_file, char* out_file) {
  CPrecomp* precomp_mgr = PrecompCreate();
  int filesize_err = 0;
  PrecompGetRecursionContext(precomp_mgr)->fin_length = fileSize64(in_file, &filesize_err);

  FILE* fin = NULL;
  if (strcmp(in_file, "stdin") == 0) {
    fin = stdin;
  } else
  {
    fin = fopen(in_file, "rb");
    if (fin == NULL) {
      printf("ERROR: Input file \"%s\" doesn't exist", in_file);
      return 1;
    }
  }

  FILE* fout = NULL;
  if (strcmp(in_file, "stdout") == 0) {
    fout = stdout;
  }
  else
  {
    fout = fopen(out_file, "a+b");
  }

  PrecompSetInputFile(precomp_mgr, fin, in_file);
  PrecompSetOutputFile(precomp_mgr, fout, out_file);
  return PrecompRecompress(precomp_mgr);
}

int main(int argc, char* argv[]) {
  char msg[256];

  if ((argc != 2) && (argc != 3)) {
    printf("\nSyntax: dlltest [d] input_file\n");
    return -1;
  }

  printf("\nCopyright message:\n");
  PrecompGetCopyrightMsg(msg);
  printf("%s\n", msg);

  int precomp_ret_code = 0;
  if (argc == 2) {
    precomp_ret_code = precompress_file(argv[1], "~temp.pcf");
    if (precomp_ret_code == 0) {
      printf("File %s was precompressed successfully to ~temp.pcf.\n", argv[1]);
    }
  } else {
    precomp_ret_code = recompress_file(argv[2], "out.dat");
    if (precomp_ret_code == 0) {
      printf("File %s was recompressed successfully to out.dat.\n", argv[2]);
    }
  }
  if (precomp_ret_code != 0) {
    printf("ERROR: Precomp returned error code %i.\n", precomp_ret_code);
  }

  return precomp_ret_code;
}
