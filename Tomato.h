#ifndef TOMATO_PN5180ISO15693_H
#define TOMATO_PN5180ISO15693_H

#include <PN5180ISO15693.h>

class TOMATO : public PN5180ISO15693 {

public:
  TOMATO(uint8_t SSpin, uint8_t BUSYpin, uint8_t RSTpin);

private:
  ISO15693ErrorCode issueISO15693Command(uint8_t *cmd, uint8_t cmdLen, uint8_t **resultPtr);
public:
  ISO15693ErrorCode getPatchInfo(uint8_t *patchInfo);
  ISO15693ErrorCode getReadings(uint8_t *readings, uint8_t blockNo);

};

#endif /* PN5180ISO15693_H */