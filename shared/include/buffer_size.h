#ifndef BUFFER_SIZE_H
#define BUFFER_SIZE_H

/* 
Only modify the buffer size header file in the shared directory!
The Makefile will copy that header to another location for the build process.
*/

#define ENCLAVE_READ_BUFFER 2000    // in KB
#define ENCLAVE_SMALL_BUFFER 10 // in KB
#define MAX_DPINAME_LENGTH 30
#define AES_KEY_LENGTH 16 // 128 bit enc
#define AES_IV_LENGTH 16 // 128 bit enc

#define ENCLAVE_READ_BUFFER_SIZE ENCLAVE_READ_BUFFER * 1024  // in B

#define ENCLAVE_SMALL_BUFFER_SIZE ENCLAVE_SMALL_BUFFER * 1024

#define RSA_PUB_KEY_SIZE 512

#define MAX_EVIDENCE_SIZE 20000 // Picked based on vibes - I have no idea how big the evidence can be!

#define TWO_BIT_INT_ARR_SIZE 4 // compressed two bit uint8_t array size (8 bits / 2 bits per value)

#define MAX_LOCI_ALLELE_STR_SIZE 28

#define EOFSeperator "~EOF~" // mark end of dataset

enum EncAnalysis { linear_dummy, linear, logistic, linear_oblivious, logistic_oblivious };
enum ImputePolicy { EPACTS, Hail };

#endif



