//
//  writeMem.h
//  libhooker
//
//  Created by CoolStar on 2/12/20.
//  Copyright © 2020 CoolStar. All rights reserved.
//

#ifndef writeMem_h
#define writeMem_h

bool LHMarkMemoryWriteable(void *data);
bool LHMarkMemoryExecutable(void *data);
bool LHExecMemory(void **page, void *data, size_t size);
bool LHWriteMemoryInternal(void *destination, const void *data, size_t size);

#endif /* writeMem_h */
