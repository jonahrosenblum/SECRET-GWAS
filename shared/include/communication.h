#ifndef _COMMUNICATION_H_
#define _COMMUNICATION_H_

#include <string>
#include <vector>

enum ClientMessageType {
  CLIENT_INFO,
  COMPUTE_INFO,
  RSA_PUB_KEY,
  Y_AND_COV,
  DATA_REQUEST,
  CLIENT_SYNC,
  END_CLIENT
};

enum ComputeServerMessageType {
  GLOBAL_ID, 
  REGISTER,
  AES_KEY,
  PATIENT_COUNT,
  COVARIANT,
  Y_VAL,
  DATA,
  EOF_DATA,
  END_COMPUTE
};

enum RegisterServerMessageType {
  COMPUTE_REGISTER,
  CLIENT_REGISTER,
  OUTPUT,
  EOF_OUTPUT
};

struct ConnectionInfo {
  std::string hostname;
  unsigned int port;
  unsigned int num_threads; // only for compute server
};

struct DataBlock {
  std::string locus;
  std::string data;
};

struct DataBlockBatch {
  std::vector<DataBlock*> blocks_batch;
  int pos;
};

#endif