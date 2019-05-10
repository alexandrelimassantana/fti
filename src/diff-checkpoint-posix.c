#include "interface.h"

/*-------------------------------------------------------------------------*/
/**
  @brief      Writes dCP ckpt to PFS using POSIX.
  @param      FTI_Conf        Configuration metadata.
  @param      FTI_Exec        Execution metadata.
  @param      FTI_Topo        Topology metadata.
  @param      FTI_Ckpt        Checkpoint metadata.
  @param      FTI_Data        Dataset metadata.
  @return     integer         FTI_SCES if successful.

  dCP POSIX implementation of FTI_WritePosix().
 **/
/*-------------------------------------------------------------------------*/
int FTI_WritePosixDcp

(
        FTIT_configuration* FTI_Conf, 
        FTIT_execution* FTI_Exec,
        FTIT_topology* FTI_Topo, 
        FTIT_checkpoint* FTI_Ckpt,
        FTIT_dataset* FTI_Data
)

{
    /*
     * TODO
     * if failure during write, we need to truncate the dCP file to the last successful
     * dCP file size, otherwise we cannot recover from future successful layers in the 
     * current dCP file.
     */ 
    char errstr[FTI_BUFS];
    FTI_Exec->dcpInfoPosix.dcpSize = 0;
    FTI_Exec->dcpInfoPosix.dataSize = 0;
    
    // dcpFileId increments every dcpStackSize checkpoints.
    int dcpFileId = FTI_Exec->dcpInfoPosix.Counter / FTI_Conf->dcpInfoPosix.StackSize;

    // dcpLayer corresponds to the additional layers towards the base layer.
    int dcpLayer = FTI_Exec->dcpInfoPosix.Counter % FTI_Conf->dcpInfoPosix.StackSize;
   
    char fn[FTI_BUFS];

    snprintf( FTI_Exec->meta[0].ckptFile, FTI_BUFS, "dcp-id%d-rank%d.fti", dcpFileId, FTI_Topo->myRank );
    if (FTI_Ckpt[4].isInline) { //If inline L4 save directly to global directory
        snprintf( fn, FTI_BUFS, "%s/%s", FTI_Ckpt[4].dcpDir, FTI_Exec->meta[0].ckptFile );
    } else {
        snprintf( fn, FTI_BUFS, "%s/%s", FTI_Ckpt[1].dcpDir, FTI_Exec->meta[0].ckptFile );
    }

    FILE *fd;
    if( dcpLayer == 0 ) {
        fd = fopen( fn, "wb" );
        if( fd == NULL ) {
            snprintf( errstr, FTI_BUFS, "Cannot create file '%s'!", fn ); 
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
    } else {
        fd = fopen( fn, "ab" );
        if( fd == NULL ) {
            snprintf( errstr, FTI_BUFS, "Cannot open file '%s' in append mode!", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
    }

    // if first layer, make sure that we write all data by setting hashdatasize = 0
    if( dcpLayer == 0 ) {
        int i = 0;
        for(; i<FTI_Exec->nbVar; i++) {
            free(FTI_Data[i].dcpInfoPosix.hashArray);
            FTI_Data[i].dcpInfoPosix.hashArray = NULL;
            FTI_Data[i].dcpInfoPosix.hashDataSize = 0;
        }
    }

    // for file hash create hash only from data block hashes
    MD5_CTX ctx;
    MD5_Init( &ctx );

    unsigned long layerSize = 0;

    unsigned char * block = (unsigned char*) malloc( FTI_Conf->dcpInfoPosix.BlockSize );
    if( !block ) {
        FTI_Print("unable to allocate memory!", FTI_EROR);
        return FTI_NSCS;
    }
    int i = 0;
    if( dcpLayer == 0 ) FTI_Exec->dcpInfoPosix.FileSize = 0;
    
    // write constant meta data in the beginning of file
    // - blocksize
    // - stacksize
    if( dcpLayer == 0 ) {
        while( !fwrite( &FTI_Conf->dcpInfoPosix.BlockSize, sizeof(unsigned long), 1, fd ) ) {
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to write in file %s", fn );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
        }
        while( !fwrite( &FTI_Conf->dcpInfoPosix.StackSize, sizeof(unsigned int), 1, fd ) ) {
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to write in file %s", fn );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
        }
        FTI_Exec->dcpInfoPosix.FileSize += sizeof(unsigned long) + sizeof(unsigned int);
        layerSize += sizeof(unsigned long) + sizeof(unsigned int);
    }
    
    // write actual amount of variables at the beginning of each layer
    while( !fwrite( &FTI_Exec->ckptID, sizeof(int), 1, fd ) ) {
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to write in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
    }
    while( !fwrite( &FTI_Exec->nbVar, sizeof(int), 1, fd ) ) {
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to write in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
    }
    FTI_Exec->dcpInfoPosix.FileSize += 2*sizeof(int);
    layerSize += 2*sizeof(int);
    
    for(; i<FTI_Exec->nbVar; i++) {
         
        FTI_Exec->dcpInfoPosix.dataSize += FTI_Data[i].size;
        
        unsigned int varId = FTI_Data[i].id;
        unsigned long dataSize = FTI_Data[i].size;
        unsigned long nbHashes = dataSize/FTI_Conf->dcpInfoPosix.BlockSize + (bool)(dataSize%FTI_Conf->dcpInfoPosix.BlockSize);
        
        if( dataSize > (MAX_BLOCK_IDX*FTI_Conf->dcpInfoPosix.BlockSize) ) {
            snprintf( errstr, FTI_BUFS, "overflow in size of dataset with id: %d (datasize: %lu > MAX_DATA_SIZE: %lu)", 
                    FTI_Data[i].id, dataSize, ((unsigned long)MAX_BLOCK_IDX)*((unsigned long)FTI_Conf->dcpInfoPosix.BlockSize) );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        if( varId > MAX_VAR_ID ) {
            snprintf( errstr, FTI_BUFS, "overflow in ID (id: %d > MAX_ID: %d)!", FTI_Data[i].id, (int)MAX_VAR_ID );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        
        // allocate tmp hash array
        FTI_Data[i].dcpInfoPosix.hashArrayTmp = (unsigned char*) malloc( sizeof(unsigned char)*nbHashes*FTI_Conf->dcpInfoPosix.digestWidth );
        if( !FTI_Data[i].dcpInfoPosix.hashArrayTmp ) {
            FTI_Print("unable to allocate memory!", FTI_EROR);
            return FTI_NSCS;
        }
        
        // create meta data buffer
        blockMetaInfo_t blockMeta;
        blockMeta.varId = FTI_Data[i].id;
       
        if( dcpLayer == 0 ) {
            while( !fwrite( &FTI_Data[i].id, sizeof(int), 1, fd ) ) {
                if(ferror(fd)) {
                    snprintf( errstr, FTI_BUFS, "unable to write in file %s", fn );
                    FTI_Print( errstr, FTI_EROR );
                    return FTI_NSCS;
                }
            }
            while( !fwrite( &dataSize, sizeof(unsigned long), 1, fd ) ) {
                if(ferror(fd)) {
                    snprintf( errstr, FTI_BUFS, "unable to write in file %s", fn );
                    FTI_Print( errstr, FTI_EROR );
                }
            }
            FTI_Exec->dcpInfoPosix.FileSize += (sizeof(int) + sizeof(unsigned long));
            layerSize += sizeof(int) + sizeof(unsigned long);
        }
        unsigned long pos = 0;
        unsigned char * ptr = FTI_Data[i].ptr;
        
        while( pos < dataSize ) {
            
            // hash index
            unsigned int blockId = pos/FTI_Conf->dcpInfoPosix.BlockSize;
            unsigned int hashIdx = blockId*FTI_Conf->dcpInfoPosix.digestWidth;
            
            blockMeta.blockId = blockId;

            unsigned int chunkSize = ( (dataSize-pos) < FTI_Conf->dcpInfoPosix.BlockSize ) ? dataSize-pos : FTI_Conf->dcpInfoPosix.BlockSize;
            unsigned int dcpChunkSize = chunkSize; 
            // compute hashes
            if( chunkSize < FTI_Conf->dcpInfoPosix.BlockSize ) {
                // if block smaller pad with zeros
                memset( block, 0x0, FTI_Conf->dcpInfoPosix.BlockSize );
                memcpy( block, ptr, chunkSize );
                FTI_Conf->dcpInfoPosix.hashFunc( block, FTI_Conf->dcpInfoPosix.BlockSize, &FTI_Data[i].dcpInfoPosix.hashArrayTmp[hashIdx] );
                ptr = block;
                chunkSize = FTI_Conf->dcpInfoPosix.BlockSize;
            } else {
                FTI_Conf->dcpInfoPosix.hashFunc( ptr, FTI_Conf->dcpInfoPosix.BlockSize, &FTI_Data[i].dcpInfoPosix.hashArrayTmp[hashIdx] );
            }
            
            bool commitBlock;
            // if old hash exists, compare. If datasize increased, there wont be an old hash to compare with.
            if( pos < FTI_Data[i].dcpInfoPosix.hashDataSize ) {
                commitBlock = memcmp( &FTI_Data[i].dcpInfoPosix.hashArray[hashIdx], &FTI_Data[i].dcpInfoPosix.hashArrayTmp[hashIdx], FTI_Conf->dcpInfoPosix.digestWidth );
            } else {
                commitBlock = true;
            }

            bool success = true;
            int fileUpdate = 0;
            if( commitBlock ) {
                if( dcpLayer > 0 ) {
                    success = (bool)fwrite( &blockMeta, 6, 1, fd );
                    if(ferror(fd)) {
                        snprintf( errstr, FTI_BUFS, "unable to write in file %s", fn );
                        FTI_Print( errstr, FTI_EROR );
                    }
                    if( success) fileUpdate += 6;
                }
                if( success ) {
                    success = (bool)fwrite( ptr, chunkSize, 1, fd );
                    if(ferror(fd)) {
                        snprintf( errstr, FTI_BUFS, "unable to write in file %s", fn );
                        FTI_Print( errstr, FTI_EROR );
                    }
                    if( success ) {
                        fileUpdate += chunkSize;
                    }
                }
                FTI_Exec->dcpInfoPosix.FileSize += success*fileUpdate;
                layerSize += success*fileUpdate;
                FTI_Exec->dcpInfoPosix.dcpSize += success*dcpChunkSize;
                if(success) {
                    MD5_Update( &ctx, &FTI_Data[i].dcpInfoPosix.hashArrayTmp[hashIdx], MD5_DIGEST_LENGTH ); 
                }
            }
            
            pos += chunkSize*success;
            ptr = FTI_Data[i].ptr + pos;
           
        }

        // swap hash arrays and free old one
        free(FTI_Data[i].dcpInfoPosix.hashArray);
        FTI_Data[i].dcpInfoPosix.hashDataSize = dataSize;
        FTI_Data[i].dcpInfoPosix.hashArray = FTI_Data[i].dcpInfoPosix.hashArrayTmp;
        FTI_Data[i].dcpInfoPosix.hashArrayTmp = NULL;

    }

    free(block);

    fsync(fileno(fd));
    fclose( fd );
   
    // create final dcp layer hash
    unsigned char LayerHash[MD5_DIGEST_LENGTH];
    MD5_Final( LayerHash, &ctx );
    FTI_GetHashHexStr( LayerHash, MD5_DIGEST_LENGTH, &FTI_Exec->dcpInfoPosix.LayerHash[dcpLayer*MD5_DIGEST_STRING_LENGTH] );

    // layer size is needed in order to create layer hash during recovery
    FTI_Exec->dcpInfoPosix.LayerSize[dcpLayer] = layerSize;

    FTI_Exec->dcpInfoPosix.Counter++;
    
    // delete old ckpt file if new dcp file is created (number of layers reached stack size)
    if( (dcpLayer == 0) ) {
        char ofn[512];
        snprintf( ofn, FTI_BUFS, "%s/dcp-id%d-rank%d.fti", FTI_Ckpt[4].dcpDir, dcpFileId-1, FTI_Topo->myRank );
        if( (remove(ofn) < 0) && (errno != ENOENT) ) {
            snprintf(errstr, FTI_BUFS, "cannot delete file '%s'", ofn );
            FTI_Print( errstr, FTI_WARN ); 
        }
    }

    return FTI_SCES;
   
}

/*-------------------------------------------------------------------------*/
/**
  @brief      It loads the checkpoint data for dcpPosix.
  @return     integer         FTI_SCES if successful.

  dCP POSIX implementation of FTI_Recover().
 **/
/*-------------------------------------------------------------------------*/
int FTI_RecoverDcpPosix
( 
        FTIT_configuration* FTI_Conf, 
        FTIT_execution* FTI_Exec, 
        FTIT_checkpoint* FTI_Ckpt, 
        FTIT_dataset* FTI_Data 
)

{
    unsigned long blockSize;
    unsigned int stackSize;
    unsigned int counter;
    int nbVarLayer;
    int ckptID;

    char errstr[FTI_BUFS];
    char fn[FTI_BUFS];
    
    snprintf( fn, FTI_BUFS, "%s/%s", FTI_Ckpt[FTI_Exec->ckptLvel].dcpDir, FTI_Exec->meta[4].ckptFile );
   
    // read base part of file
    FILE* fd = fopen( fn, "rb" );
    fread( &blockSize, sizeof(unsigned long), 1, fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    fread( &stackSize, sizeof(unsigned int), 1, fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    
    // check if settings are correct. If not correct them
    if( blockSize != FTI_Conf->dcpInfoPosix.BlockSize )
    {
        char str[FTI_BUFS];
        snprintf( str, FTI_BUFS, "dCP blocksize differ between configuration settings ('%lu') and checkpoint file ('%lu')", FTI_Conf->dcpInfoPosix.BlockSize, blockSize );
        FTI_Print( str, FTI_WARN );
        return FTI_NREC;
    }
    if( stackSize != FTI_Conf->dcpInfoPosix.StackSize )
    {
        char str[FTI_BUFS];
        snprintf( str, FTI_BUFS, "dCP stacksize differ between configuration settings ('%u') and checkpoint file ('%u')", FTI_Conf->dcpInfoPosix.StackSize, stackSize );
        FTI_Print( str, FTI_WARN );
        return FTI_NREC;
    }

    
    void *buffer = (void*) malloc( blockSize ); 
    if( !buffer ) {
        FTI_Print("unable to allocate memory!", FTI_EROR);
        return FTI_NSCS;
    }

    int i;
    // treat Layer 0 first
    fread( &ckptID, 1, sizeof(int), fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    fread( &nbVarLayer, 1, sizeof(int), fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    for(i=0; i<nbVarLayer; i++) {
        unsigned int varId;
        unsigned long locDataSize;
        fread( &varId, sizeof(int), 1, fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        fread( &locDataSize, sizeof(unsigned long), 1, fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        int idx = FTI_DataGetIdx(varId, FTI_Exec, FTI_Data);
        if( idx < 0 ) {
            snprintf(errstr, FTI_BUFS, "id '%d' does not exist!", varId);
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        fread( FTI_Data[idx].ptr, locDataSize, 1, fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }

        int overflow;
        if( (overflow=locDataSize%blockSize) != 0 ) {
            fread( buffer, blockSize - overflow, 1, fd );
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
        }
    }


    unsigned long offset;

    blockMetaInfo_t blockMeta;
    unsigned char *block = (unsigned char*) malloc( blockSize );
    if( !block ) {
        FTI_Print("unable to allocate memory!", FTI_EROR);
        return FTI_NSCS;
    }

    int nbLayer = FTI_Exec->dcpInfoPosix.nbLayerReco;
    for( i=1; i<nbLayer; i++) {

        unsigned long pos = 0;
        pos += fread( &ckptID, 1, sizeof(int), fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        pos += fread( &nbVarLayer, 1, sizeof(int), fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }

        while( pos < FTI_Exec->dcpInfoPosix.LayerSize[i] ) {

            fread( &blockMeta, 1, 6, fd );
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
            int idx = FTI_DataGetIdx(blockMeta.varId, FTI_Exec, FTI_Data);
            if( idx < 0 ) {
                snprintf(errstr, FTI_BUFS, "id '%d' does not exist!", blockMeta.varId);
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }


            offset = blockMeta.blockId * blockSize;
            void* ptr = FTI_Data[idx].ptr + offset;
            unsigned int chunkSize = ( (FTI_Data[idx].size-offset) < blockSize ) ? FTI_Data[idx].size-offset : blockSize; 

            fread( ptr, 1, chunkSize, fd );
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
            fread( buffer, 1, blockSize - chunkSize, fd ); 
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }

            pos += (blockSize+6);
        }

    }

    // create hasharray
    for(i=0; i<FTI_Exec->nbVar; i++) {
        unsigned long nbBlocks = (FTI_Data[i].size % blockSize) ? FTI_Data[i].size/blockSize + 1 : FTI_Data[i].size/blockSize;
        FTI_Data[i].dcpInfoPosix.hashDataSize = FTI_Data[i].size;
        FTI_Data[i].dcpInfoPosix.hashArray = realloc(FTI_Data[i].dcpInfoPosix.hashArray, nbBlocks*MD5_DIGEST_LENGTH);
        int j;
        for(j=0; j<nbBlocks-1; j++) {
            unsigned long offset = j*blockSize;
            unsigned long hashIdx = j*MD5_DIGEST_LENGTH;
            MD5( FTI_Data[i].ptr+offset, blockSize, &FTI_Data[i].dcpInfoPosix.hashArray[hashIdx] );
        }
        if( FTI_Data[i].size%blockSize ) {
            unsigned char* buffer = calloc( 1, blockSize );
            if( !buffer ) {
                FTI_Print("unable to allocate memory!", FTI_EROR);
                return FTI_NSCS;
            }
            unsigned long dataOffset = blockSize * (nbBlocks - 1);
            unsigned long dataSize = FTI_Data[i].size - dataOffset;
            memcpy( buffer, FTI_Data[i].ptr + dataOffset, dataSize ); 
            MD5( buffer, blockSize, &FTI_Data[i].dcpInfoPosix.hashArray[(nbBlocks-1)*MD5_DIGEST_LENGTH] );
        }
    }


    FTI_Exec->reco = 0;

    free(buffer);
    fclose(fd);

    return FTI_SCES;

}

/*-------------------------------------------------------------------------*/
/**
  @brief      Recovers the given variable for dcpPosix
  @param      id              Variable to recover
  @return     int             FTI_SCES if successful.

  dCP POSIX implementation of FTI_RecoverVar().
 **/
/*-------------------------------------------------------------------------*/
int FTI_RecoverVarDcpPosix
( 
        FTIT_configuration* FTI_Conf, 
        FTIT_execution* FTI_Exec, 
        FTIT_checkpoint* FTI_Ckpt, 
        FTIT_dataset* FTI_Data,
        int id
)

{
    unsigned long blockSize;
    unsigned int stackSize;
    unsigned int counter;
    int nbVarLayer;
    int ckptID;

    char errstr[FTI_BUFS];
    char fn[FTI_BUFS];

    snprintf( fn, FTI_BUFS, "%s/%s", FTI_Ckpt[FTI_Exec->ckptLvel].dcpDir, FTI_Exec->meta[4].ckptFile );

    // read base part of file
    FILE* fd = fopen( fn, "rb" );
    fread( &blockSize, sizeof(unsigned long), 1, fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    fread( &stackSize, sizeof(unsigned int), 1, fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }

    // check if settings are correct. If not correct them
    if( blockSize != FTI_Conf->dcpInfoPosix.BlockSize )
    {
        char str[FTI_BUFS];
        snprintf( str, FTI_BUFS, "dCP blocksize differ between configuration settings ('%lu') and checkpoint file ('%lu')", FTI_Conf->dcpInfoPosix.BlockSize, blockSize );
        FTI_Print( str, FTI_WARN );
        return FTI_NREC;
    }
    if( stackSize != FTI_Conf->dcpInfoPosix.StackSize )
    {
        char str[FTI_BUFS];
        snprintf( str, FTI_BUFS, "dCP stacksize differ between configuration settings ('%u') and checkpoint file ('%u')", FTI_Conf->dcpInfoPosix.StackSize, stackSize );
        FTI_Print( str, FTI_WARN );
        return FTI_NREC;
    }


    void *buffer = (void*) malloc( blockSize ); 
    if( !buffer ) {
        FTI_Print("unable to allocate memory!", FTI_EROR);
        return FTI_NSCS;
    }

    int i;

    // treat Layer 0 first
    fread( &ckptID, 1, sizeof(int), fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    fread( &nbVarLayer, 1, sizeof(int), fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    for(i=0; i<nbVarLayer; i++) {
        unsigned int varId;
        unsigned long locDataSize;
        fread( &varId, sizeof(int), 1, fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        fread( &locDataSize, sizeof(unsigned long), 1, fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        // if requested id load else skip dataSize
        if( varId == id ) {
            int idx = FTI_DataGetIdx(varId, FTI_Exec, FTI_Data);
            if( idx < 0 ) {
                snprintf(errstr, FTI_BUFS, "id '%d' does not exist!", varId);
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
            fread( FTI_Data[idx].ptr, locDataSize, 1, fd );
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }

            int overflow;
            if( (overflow=locDataSize%blockSize) != 0 ) {
                fread( buffer, blockSize - overflow, 1, fd );
                if(ferror(fd)) {
                    snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
                    FTI_Print( errstr, FTI_EROR );
                    return FTI_NSCS;
                }
            }
        } else {
            unsigned long skip = ( locDataSize%blockSize == 0 ) ? locDataSize : (locDataSize/blockSize + 1)*blockSize;
            if( fseek( fd, skip, SEEK_CUR ) == -1 ) {
                snprintf( errstr, FTI_BUFS, "unable to seek in file %s", fn );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
        }
    }


    unsigned long offset;

    blockMetaInfo_t blockMeta;
    unsigned char *block = (unsigned char*) malloc( blockSize );
    if( !block ) {
        FTI_Print("unable to allocate memory!", FTI_EROR);
        return FTI_NSCS;
    }

    int nbLayer = FTI_Exec->dcpInfoPosix.nbLayerReco;
    for( i=1; i<nbLayer; i++) {

        unsigned long pos = 0;
        pos += fread( &ckptID, 1, sizeof(int), fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        pos += fread( &nbVarLayer, 1, sizeof(int), fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }

        while( pos < FTI_Exec->dcpInfoPosix.LayerSize[i] ) {

            fread( &blockMeta, 1, 6, fd );
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
            if( blockMeta.varId == id ) {
                int idx = FTI_DataGetIdx(blockMeta.varId, FTI_Exec, FTI_Data);
                if( idx < 0 ) {
                    snprintf(errstr, FTI_BUFS, "id '%d' does not exist!", blockMeta.varId);
                    FTI_Print( errstr, FTI_EROR );
                    return FTI_NSCS;
                }


                offset = blockMeta.blockId * blockSize;
                void* ptr = FTI_Data[idx].ptr + offset;
                unsigned int chunkSize = ( (FTI_Data[idx].size-offset) < blockSize ) ? FTI_Data[idx].size-offset : blockSize; 

                fread( ptr, 1, chunkSize, fd );
                if(ferror(fd)) {
                    snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
                    FTI_Print( errstr, FTI_EROR );
                    return FTI_NSCS;
                }
                fread( buffer, 1, blockSize - chunkSize, fd ); 
                if(ferror(fd)) {
                    snprintf( errstr, FTI_BUFS, "unable to read in file %s", fn );
                    FTI_Print( errstr, FTI_EROR );
                    return FTI_NSCS;
                }

            } else {
                if( fseek( fd, blockSize, SEEK_CUR ) == -1 ) {
                    snprintf( errstr, FTI_BUFS, "unable to seek in file %s", fn );
                    FTI_Print( errstr, FTI_EROR );
                    return FTI_NSCS;
                }
            }
            pos += (blockSize+6);
        }

    }

    // create hasharray for id
    i = FTI_DataGetIdx( id, FTI_Exec, FTI_Data );
    unsigned long nbBlocks = (FTI_Data[i].size % blockSize) ? FTI_Data[i].size/blockSize + 1 : FTI_Data[i].size/blockSize;
    FTI_Data[i].dcpInfoPosix.hashDataSize = FTI_Data[i].size;
    FTI_Data[i].dcpInfoPosix.hashArray = realloc(FTI_Data[i].dcpInfoPosix.hashArray, nbBlocks*MD5_DIGEST_LENGTH);
    int j;
    for(j=0; j<nbBlocks-1; j++) {
        unsigned long offset = j*blockSize;
        unsigned long hashIdx = j*MD5_DIGEST_LENGTH;
        MD5( FTI_Data[i].ptr+offset, blockSize, &FTI_Data[i].dcpInfoPosix.hashArray[hashIdx] );
    }
    if( FTI_Data[i].size%blockSize ) {
        unsigned char* buffer = calloc( 1, blockSize );
        if( !buffer ) {
            FTI_Print("unable to allocate memory!", FTI_EROR);
            return FTI_NSCS;
        }
        unsigned long dataOffset = blockSize * (nbBlocks - 1);
        unsigned long dataSize = FTI_Data[i].size - dataOffset;
        memcpy( buffer, FTI_Data[i].ptr + dataOffset, dataSize ); 
        MD5( buffer, blockSize, &FTI_Data[i].dcpInfoPosix.hashArray[(nbBlocks-1)*MD5_DIGEST_LENGTH] );
    }

    free(buffer);
    fclose(fd);

    return FTI_SCES;

}

/*-------------------------------------------------------------------------*/
/**
  @brief      It checks if a file exist and that its size is 'correct'.
  @param      fn              The ckpt. file name to check.
  @param      fs              The ckpt. file size to check.
  @param      checksum        The file checksum to check.
  @return     integer         0 if file exists, 1 if not or wrong size.

  dCP POSIX implementation of FTI_CheckFile().
 **/
/*-------------------------------------------------------------------------*/
int FTI_CheckFileDcpPosix
(
        char* fn, 
        long fs, 
        char* checksum
)

{
    if (access(fn, F_OK) == 0) {
        struct stat fileStatus;
        if (stat(fn, &fileStatus) == 0) {
            if (fileStatus.st_size == fs) {
                if (strlen(checksum)) {
                    int res = FTI_VerifyChecksumDcpPosix(fn);
                    if (res != FTI_SCES) {
                        return 1;
                    }
                    return 0;
                }
                return 0;
            }
            else {
                return 1;
            }
        }
        else {
            return 1;
        }
    }
    else {
        char str[FTI_BUFS];
        sprintf(str, "Missing file: \"%s\"", fn);
        FTI_Print(str, FTI_WARN);
        return 1;
    }
}

/*-------------------------------------------------------------------------*/
/**
  @brief      It compares checksum of the checkpoint file.
  @param      fileName        Filename of the checkpoint.
  @param      checksumToCmp   Checksum to compare.
  @return     integer         FTI_SCES if successful.

  dCP POSIX implementation of FTI_VerifyChecksum().
 **/
/*-------------------------------------------------------------------------*/
int FTI_VerifyChecksumDcpPosix
(
        char* fileName
)

{

    FTIT_execution* exec = FTI_DcpPosixRecoverRuntimeInfo( DCP_POSIX_EXEC_TAG, NULL, NULL );
    FTIT_configuration* conf = FTI_DcpPosixRecoverRuntimeInfo( DCP_POSIX_CONF_TAG, NULL, NULL ); 

    char errstr[FTI_BUFS];
    char dummyBuffer[FTI_BUFS];
    char checksumToCmp[MD5_DIGEST_LENGTH];
    unsigned long blockSize;
    unsigned int stackSize;
    unsigned int counter;
    unsigned int dcpFileId;
    int nbVarBasis;

    FILE *fd = fopen(fileName, "rb");
    if (fd == NULL) {
        char str[FTI_BUFS];
        sprintf(str, "FTI failed to open file %s to calculate checksum.", fileName);
        FTI_Print(str, FTI_WARN);
        return FTI_NSCS;
    }

    unsigned char md5_tmp[MD5_DIGEST_LENGTH];
    unsigned char md5_final[MD5_DIGEST_LENGTH];
    unsigned char md5_string[MD5_DIGEST_STRING_LENGTH];

    MD5_CTX mdContext;

    // position in file
    size_t fs = 0;

    // get blocksize
    fs += fread( &blockSize, 1, sizeof(unsigned long), fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    fs += fread( &stackSize, 1, sizeof(unsigned int), fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }

    // check if settings are correckt. If not correct them
    if( blockSize != conf->dcpInfoPosix.BlockSize )
    {
        char str[FTI_BUFS];
        snprintf( str, FTI_BUFS, "dCP blocksize differ between configuration settings ('%lu') and checkpoint file ('%lu')", conf->dcpInfoPosix.BlockSize, blockSize );
        FTI_Print( str, FTI_WARN );
        conf->dcpInfoPosix.BlockSize = blockSize;
    }
    if( stackSize != conf->dcpInfoPosix.StackSize )
    {
        char str[FTI_BUFS];
        snprintf( str, FTI_BUFS, "dCP stacksize differ between configuration settings ('%u') and checkpoint file ('%u')", conf->dcpInfoPosix.StackSize, stackSize );
        FTI_Print( str, FTI_WARN );
        conf->dcpInfoPosix.StackSize = stackSize;
    }

    // get dcpFileId from filename
    int dummy;
    sscanf( exec->meta[4].ckptFile, "dcp-id%d-rank%d.fti", &dcpFileId, &dummy );
    counter = dcpFileId * stackSize;

    int i;
    int layer = 0;
    int nbVarLayer;
    int ckptID;

    // set number of recovered layers to 0
    exec->dcpInfoPosix.nbLayerReco = 0;

    // data buffer
    void* buffer = malloc( blockSize );
    if( !buffer ) {
        FTI_Print("unable to allocate memory!", FTI_EROR);
        return FTI_NSCS;
    }

    // check layer 0 first
    // get number of variables stored in layer
    MD5_Init( &mdContext );
    fs += fread( &ckptID, 1, sizeof(int), fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    fs += fread( &nbVarLayer, 1, sizeof(int), fd );
    if(ferror(fd)) {
        snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
        FTI_Print( errstr, FTI_EROR );
        return FTI_NSCS;
    }
    for(i=0; i<nbVarLayer; i++) {
        unsigned long dataSize;
        unsigned long pos = 0;
        fs += fread( dummyBuffer, 1, sizeof(int), fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        fs += fread( &dataSize, 1, sizeof(unsigned long), fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        while( pos < dataSize ) {
    //DBG_MSG("pos: %lu, dataSize: %lu",-1, pos, dataSize);
            pos += fread( buffer, 1, blockSize, fd );
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
            MD5( buffer, blockSize, md5_tmp );
            MD5_Update( &mdContext, md5_tmp, MD5_DIGEST_LENGTH );
        }
        fs += pos;
    }
    MD5_Final( md5_final, &mdContext );
    // compare hashes
    if( strcmp( FTI_GetHashHexStr( md5_final, conf->dcpInfoPosix.digestWidth, NULL ), &exec->dcpInfoPosix.LayerHash[layer*MD5_DIGEST_STRING_LENGTH] ) ) {
        FTI_Print("hashes differ in base", FTI_WARN);
        return FTI_NSCS;
    }
    layer++;
    exec->dcpInfoPosix.nbLayerReco = layer;
    exec->dcpInfoPosix.nbVarReco = nbVarLayer;
    exec->ckptID = ckptID;
    counter++;
    //exec->dcpInfoPosix.Counter = counter;

    // now treat other layers
    for(; layer<stackSize; layer++) {
        MD5_Init( &mdContext );
        unsigned long layerSize = fread( &ckptID, 1, sizeof(int), fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        if (feof(fd)) break;
        layerSize += fread( &nbVarLayer, 1, sizeof(int), fd );
        if(ferror(fd)) {
            snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
            FTI_Print( errstr, FTI_EROR );
            return FTI_NSCS;
        }
        while( layerSize < exec->dcpInfoPosix.LayerSize[layer] ) {
            layerSize += fread( dummyBuffer, 1, 6, fd );
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
            layerSize += fread( buffer, 1, blockSize, fd );
            if(ferror(fd)) {
                snprintf( errstr, FTI_BUFS, "unable to read in file %s", fileName );
                FTI_Print( errstr, FTI_EROR );
                return FTI_NSCS;
            }
            MD5( buffer, blockSize, md5_tmp );
            MD5_Update( &mdContext, md5_tmp, MD5_DIGEST_LENGTH ); 
        }
        MD5_Final( md5_final, &mdContext );
        // compare hashes
        if( strcmp( FTI_GetHashHexStr( md5_final, conf->dcpInfoPosix.digestWidth, NULL ), &exec->dcpInfoPosix.LayerHash[layer*MD5_DIGEST_STRING_LENGTH] ) ) {
            FTI_Print("hashes differ in layer", FTI_WARN);
            break;
        }

        fs += layerSize;
        exec->dcpInfoPosix.nbLayerReco = layer+1;
        exec->dcpInfoPosix.nbVarReco = nbVarLayer;
        exec->ckptID = ckptID;
        counter++;
    }

    exec->dcpInfoPosix.Counter = counter;

    // truncate file if some layer were not possible to recover.
    ftruncate(fileno(fd), fs);
    fclose (fd);

    return FTI_SCES;
}

// HELPER FUNCTIONS

void* FTI_DcpPosixRecoverRuntimeInfo( int tag, void* exec_, void* conf_ ) {
    
    static void* exec = NULL;
    static void* conf = NULL;
    
    void* ret;

    switch( tag ) {
        case DCP_POSIX_EXEC_TAG:
            ret = exec;
            break;
        case DCP_POSIX_CONF_TAG:
            ret = conf;
            break;
        case DCP_POSIX_INIT_TAG:
            ret = NULL;
            exec = exec_;
            conf = conf_;
            break;
    }

    return ret;
}

// have the same for for MD5 and CRC32
unsigned char* CRC32( const unsigned char *d, unsigned long nBytes, unsigned char *hash )
{
    static unsigned char hash_[CRC32_DIGEST_LENGTH];
    if( hash == NULL ) {
        hash = hash_;
    }
    
    uint32_t digest = crc32( 0L, Z_NULL, 0 );
    digest = crc32( digest, d, nBytes );

    memcpy( hash, &digest, CRC32_DIGEST_LENGTH );

    return hash;
}

int FTI_DataGetIdx( int varId, FTIT_execution* FTI_Exec, FTIT_dataset* FTI_Data )
{
    int i=0;
    for(; i<FTI_Exec->nbVar; i++) {
        if(FTI_Data[i].id == varId) break;
    }
    if( i==FTI_Exec->nbVar ) {
        return -1;
    }
    return i;
}


