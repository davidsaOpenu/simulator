//#ifndef SIMLOGS
//#define SIMLOGS

#include "json-maker.c"
#include <time.h>

#define MAX_LOG_SIZE 512

// Working with string time stamps, differnt from logging_parser.h
// Changed structs definitions slightly from logging_parser.h, removed typedef
// Useful for converting timestamps from string to int https://gchq.github.io/CyberChef/#recipe=To_UNIX_Timestamp('Seconds%20(s)',true,true)From_UNIX_Timestamp('Seconds%20(s)'/disabled)&input=MjAxOS0xMi0yMCAxMDo0ODoxMg
typedef struct LogMetadata{
    /**
     * Logging start time
     */
    char* logging_start_time;
    /**
     * Logging end time
     */
    char* logging_end_time;
} LogMetadata;

char* json_LogMetadata( char* dest, char const* name, struct LogMetadata const* LogMetadata ) {
    dest = json_objOpen( dest, name );    
    char* logging_start_time = asctime(gmtime(&LogMetadata->logging_start_time));
    char* logging_end_time = asctime(gmtime(&LogMetadata->logging_end_time));
    dest = json_nstr( dest, "logging_start_time", logging_start_time, strlen(logging_start_time)); 
    dest = json_nstr( dest, "logging_end_time", logging_end_time, strlen(logging_end_time) );   
    dest = json_objClose( dest );  
    return dest;
}

/**
 * A log of a physical cell read
 */
 typedef struct PhysicalCellReadLog{
    /**
     * The channel number of the cell read
     */
    unsigned int channel;
    /**
     * The block number of the cell read
     */
    unsigned int block;
    /**
     * The page number of the cell read
     */
    unsigned int page;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} PhysicalCellReadLog;


char* json_PhysicalCellReadLog( char* dest, const int channel,const  int block, const int page, const char* logging_start_time, const char* logging_end_time ) {

         struct PhysicalCellReadLog const PhysicalCellReadLog = {
                .channel  = channel,
                .block = block,
                .page = page,
                .metadata={
                  .logging_start_time=logging_start_time,
                  .logging_end_time=logging_end_time
                }
    };
    dest = json_objOpen( dest, "PhysicalCellReadLog" );             
    dest = json_int( dest, "channel", PhysicalCellReadLog->channel );
    dest = json_int( dest, "block", PhysicalCellReadLog->block );  
    dest = json_int( dest, "page", PhysicalCellReadLog->page );   
    dest = json_LogMetadata(dest, "metadata", &PhysicalCellReadLog->metadata);
    dest = json_objClose( dest );  
    char* p = json_end(dest);
    return dest - p;
}



/**
 * A log of a physical cell program
 */
 typedef struct PhysicalCellProgramLog{
    /**
     * The channel number of the programmed cell
     */
    unsigned int channel;
    /**
     * The block number of the programmed cell
     */
    unsigned int block;
    /**
     * The page number of the programmed cell
     */
    unsigned int page;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} PhysicalCellProgramLog;

char* json_PhysicalCellProgramLog( char* dest,  struct PhysicalCellProgramLog const* PhysicalCellProgramLog ) {
    dest = json_objOpen( dest, "PhysicalCellProgramLog" );             
    dest = json_int( dest, "channel", PhysicalCellProgramLog->channel );
    dest = json_int( dest, "block", PhysicalCellProgramLog->block );  
    dest = json_int( dest, "page", PhysicalCellProgramLog->page );   
    dest = json_LogMetadata(dest, "metadata", &PhysicalCellProgramLog->metadata);
    dest = json_objClose( dest );  
    char* p = json_end(dest);
    return dest - p;
}

/**
 * A log of a logical cell program
 */
// This is log needs to be changed. Logical Cell Program is actually contains CHANNEL_SWITCH_TO_WRITE, PHYSICAL_CELL_PROGRAM, BLOCK_ERASE
// So currently it will be empty log, maybe in the future it will contain more information, depends on th 
 struct LogicalCellProgramLog{
    /**
     * The channel number of the programmed cell
     */
    unsigned int channel;
    /**
     * The block number of the programmed cell
     */
    unsigned int block;
    /**
     * The page number of the programmed cell
     */
    unsigned int page;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} ;

char* json_LogicalCellProgramLog( char* dest,  struct LogicalCellProgramLog const* LogicalCellProgramLog ) {
    dest = json_objOpen( dest, "LogicalCellProgramLog" );             
    dest = json_int( dest, "channel", LogicalCellProgramLog->channel );
    dest = json_int( dest, "block", LogicalCellProgramLog->block );  
    dest = json_int( dest, "page", LogicalCellProgramLog->page );   
    dest = json_LogMetadata(dest, "metadata", &LogicalCellProgramLog->metadata);
    dest = json_objClose( dest );  
    char* p = json_end(dest);
    return dest - p;
}

/**
 * A log of a register read
 */
struct RegisterReadLog{
    /**
     * The channel number of the register read
     */
    unsigned int channel;
    /**
     * The die number of the register read
     */
    unsigned int die;
    /**
     * The register number of the register read
     */
    unsigned int reg;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} ;

char* json_RegisterReadLog( char* dest,  struct RegisterReadLog const* RegisterReadLog ) {
    dest = json_objOpen( dest, "RegisterReadLog" );             
    dest = json_int( dest, "channel", RegisterReadLog->channel );
    dest = json_int( dest, "die", RegisterReadLog->die );  
    dest = json_int( dest, "reg", RegisterReadLog->reg );   
    dest = json_LogMetadata(dest, "metadata", &RegisterReadLog->metadata);
    dest = json_objClose( dest );  
    char* p = json_end(dest);
    return dest - p;
}
/**
 * A log of a register write
 */
 struct RegisterWriteLog {
    /**
     * The channel number of the written register
     */
    unsigned int channel;
    /**
     * The die number of the written register
     */
    unsigned int die;
    /**
     * The register number of the written register
     */
    unsigned int reg;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} ;

char* json_RegisterWriteLog( char* dest,  struct RegisterWriteLog const* RegisterWriteLog ) {
    dest = json_objOpen( dest, "RegisterWriteLog" );             
    dest = json_int( dest, "channel", RegisterWriteLog->channel );
    dest = json_int( dest, "die", RegisterWriteLog->die );  
    dest = json_int( dest, "reg", RegisterWriteLog->reg );   
    dest = json_LogMetadata(dest, "metadata", &RegisterWriteLog->metadata);
    dest = json_objClose( dest );  
    char* p = json_end(dest);
    return dest - p;
}

/**
 * A log of a block erase
 */
 struct BlockEraseLog {
    /**
     * The channel number of the erased block
     */
    unsigned int channel;
    /**
     * The die number of the erased block
     */
    unsigned int die;
    /**
     * The block number of the erased block
     */
    unsigned int block;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} ;

char* json_BlockEraseLog( char* dest,  struct BlockEraseLog const* BlockEraseLog ) {
    dest = json_objOpen( dest, "BlockEraseLog" );             
    dest = json_int( dest, "channel", BlockEraseLog->channel );
    dest = json_int( dest, "die", BlockEraseLog->die );  
    dest = json_int( dest, "block", BlockEraseLog->block );   
    dest = json_LogMetadata(dest, "metadata", &BlockEraseLog->metadata);
    dest = json_objClose( dest );  
    char* p = json_end(dest);
    return dest - p;
}

/**
 * A block of a channel switch to read mode
 */
 struct ChannelSwitchToReadLog{
    /**
     * The channel number of the channel being switched
     */
    unsigned int channel;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} ;

char* json_ChannelSwitchToReadLog( char* dest,  struct ChannelSwitchToReadLog const* ChannelSwitchToReadLog ) {
    dest = json_objOpen( dest, "ChannelSwitchToReadLog" );             
    dest = json_int( dest, "channel", ChannelSwitchToReadLog->channel );
    dest = json_LogMetadata(dest, "metadata", &ChannelSwitchToReadLog->metadata);
    dest = json_objClose( dest );  
    char* p = json_end(dest);
    return dest - p;
}

/**
 * A block of a channel switch to write mode
 */
 struct ChannelSwitchToWriteLog {
    /**
     * The channel number of the channel being switched
     */
    unsigned int channel;
    /**
     * Log metadata
     */
    LogMetadata metadata;
} ;

char* json_ChannelSwitchToWriteLog( char* dest,  struct ChannelSwitchToWriteLog const* ChannelSwitchToWriteLog ) {
    dest = json_objOpen( dest, "ChannelSwitchToWriteLog" );             
    dest = json_int( dest, "channel", ChannelSwitchToWriteLog->channel );
    dest = json_LogMetadata(dest, "metadata", &ChannelSwitchToWriteLog->metadata);
    dest = json_objClose( dest );  
    char* p = json_end(dest);
    return dest - p;
}

