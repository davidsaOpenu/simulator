//#ifndef SIMLOGS
//#define SIMLOGS

#include "json-maker.c"
#include <time.h>

char* WRONG_PARAMS_ERROR = "Wrong Params";
#define MAX_LOG_SIZE 512

// Working with string time stamps, differnt from logging_parser.h
// Changed structs definitions slightly from logging_parser.h, removed typedef
// Useful for converting timestamps from string to int https://gchq.github.io/CyberChef/#recipe=To_UNIX_Timestamp('Seconds%20(s)',true,true)From_UNIX_Timestamp('Seconds%20(s)'/disabled)&input=MjAxOS0xMi0yMCAxMDo0ODoxMg
typedef struct LogMetadata{
    /**
     * Logging start time
     */
    int64_t logging_start_time;
    /**
     * Logging end time
     */
    int64_t logging_end_time;
} LogMetadata;

char* timestamp_to_str(int64_t cur_ts){
    //setenv("TZ", "GMT+1", 1);
    time_t now;
    struct tm  ts;
    static char* buf[80];

    ts = *localtime(&cur_ts);

    strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S", &ts);
    //printf("%s", buf);
    return buf;
}

char* json_LogMetadata( char* dest, char const* name, struct LogMetadata const* LogMetadata, int64_t logging_start_time, int64_t logging_end_time) {
    dest = json_objOpen( dest, name );    

    if ((logging_start_time != NULL) && (logging_end_time != NULL)){
        char* logging_start_time = timestamp_to_str(logging_start_time);
        char* logging_end_time = timestamp_to_str(logging_end_time);
        dest = json_nstr( dest, "logging_start_time", logging_start_time, strlen(logging_start_time)); 
        dest = json_nstr( dest, "logging_end_time", logging_end_time, strlen(logging_end_time) );       
    }

    // For converting time from int to string
   

    else if (LogMetadata != NULL){
        char* logging_start_time = timestamp_to_str(&LogMetadata->logging_start_time);
        char* logging_end_time = timestamp_to_str(&LogMetadata->logging_end_time);

        dest = json_nstr( dest, "logging_start_time", LogMetadata->logging_start_time, strlen(LogMetadata->logging_start_time)); 
        dest = json_nstr( dest, "logging_end_time", LogMetadata->logging_end_time, strlen(LogMetadata->logging_end_time) );       
    }
    dest = json_objClose( dest );  
    return dest;
}

/**
 * A log of a physical cell read
 */
 struct PhysicalCellReadLog{
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
} ;


char* json_PhysicalCellReadLog( char* dest, struct PhysicalCellReadLog const* PhysicalCellReadLog, const int channel,const  int block, const int page, const char* logging_start_time, const char* logging_end_time ) {
    /*struct PhysicalCellReadLog log = {
                .channel  = i_channel,
                .block = block,
                .page = page,
                .metadata={
                  .logging_start_time=logging_start_time,
                  .logging_end_time=logging_end_time
                }
    };*/
    dest = json_objOpen( dest, "PhysicalCellReadLog" );             

    if ((channel != NULL) && (block != NULL) && (page != NULL) && (logging_start_time != NULL) && (logging_end_time != NULL)){
        dest = json_int( dest, "channel", channel );
        dest = json_int( dest, "block", block );  
        dest = json_int( dest, "page", page );
        //puts(logging_start_time);
        //puts(logging_end_time);
        dest = json_LogMetadata(dest, "metadata", NULL, logging_start_time, logging_end_time);
    }
    else if(PhysicalCellReadLog != NULL){
        dest = json_int( dest, "channel", PhysicalCellReadLog->channel );
        dest = json_int( dest, "block", PhysicalCellReadLog->block );  
        dest = json_int( dest, "page", PhysicalCellReadLog->page );   
        dest = json_LogMetadata(dest, "metadata", &PhysicalCellReadLog->metadata, NULL, NULL);
    }
    else{
        
        printf("Error log Parameters to PhysicalCellReadLog");
    }
    
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

char* json_PhysicalCellProgramLog( char* dest, struct PhysicalCellProgramLog const* PhysicalCellProgramLog, const int channel,const  int block, const int page, const char* logging_start_time, const char* logging_end_time ) {
    dest = json_objOpen( dest, "PhysicalCellProgramLog" );             

    if ((channel != NULL) && (block != NULL) && (page != NULL) && (logging_start_time != NULL) && (logging_end_time != NULL)){
        dest = json_int( dest, "channel", channel );
        dest = json_int( dest, "block", block );  
        dest = json_int( dest, "page", page );
        dest = json_LogMetadata(dest, "metadata", NULL, logging_start_time, logging_end_time);
    }
    else if(PhysicalCellProgramLog != NULL){
        dest = json_int( dest, "channel", PhysicalCellProgramLog->channel );
        dest = json_int( dest, "block", PhysicalCellProgramLog->block );  
        dest = json_int( dest, "page", PhysicalCellProgramLog->page );   
        dest = json_LogMetadata(dest, "metadata", &PhysicalCellProgramLog->metadata, NULL, NULL);
    }
    else{
        
        printf("Error log Parameters to PhysicalCellProgramLog");
    }
    
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

char* json_LogicalCellProgramLog( char* dest, struct LogicalCellProgramLog const* LogicalCellProgramLog, const int channel,const  int block, const int page, const char* logging_start_time, const char* logging_end_time ) {
    dest = json_objOpen( dest, "LogicalCellProgramLog" );             

    if ((channel != NULL) && (block != NULL) && (page != NULL) && (logging_start_time != NULL) && (logging_end_time != NULL)){
        dest = json_int( dest, "channel", channel );
        dest = json_int( dest, "block", block );  
        dest = json_int( dest, "page", page );
        dest = json_LogMetadata(dest, "metadata", NULL, logging_start_time, logging_end_time);
    }
    else if(LogicalCellProgramLog != NULL){
        dest = json_int( dest, "channel", LogicalCellProgramLog->channel );
        dest = json_int( dest, "block", LogicalCellProgramLog->block );  
        dest = json_int( dest, "page", LogicalCellProgramLog->page );   
        dest = json_LogMetadata(dest, "metadata", &LogicalCellProgramLog->metadata, NULL, NULL);
    }
    else{
        
        printf("Error log Parameters to LogicalCellProgramLog");
    }
    
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

char* json_RegisterReadLog( char* dest, struct RegisterReadLog const* RegisterReadLog, const int channel,const  int die, const int reg, const char* logging_start_time, const char* logging_end_time ) {
    dest = json_objOpen( dest, "RegisterReadLog" );             

    if ((channel != NULL) && (die != NULL) && (reg != NULL) && (logging_start_time != NULL) && (logging_end_time != NULL)){
        dest = json_int( dest, "channel", channel );
        dest = json_int( dest, "die", die );  
        dest = json_int( dest, "reg", reg );
        dest = json_LogMetadata(dest, "metadata", NULL, logging_start_time, logging_end_time);
    }
    else if(RegisterReadLog != NULL){
        dest = json_int( dest, "channel", RegisterReadLog->channel );
        dest = json_int( dest, "die", RegisterReadLog->die );  
        dest = json_int( dest, "reg", RegisterReadLog->reg );   
        dest = json_LogMetadata(dest, "metadata", &RegisterReadLog->metadata, NULL, NULL);
    }
    else{
        
        printf("Error log Parameters to RegisterReadLog");
    }
    
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

char* json_RegisterWriteLog( char* dest, struct RegisterWriteLog const* RegisterWriteLog, const int channel,const  int die, const int reg, const char* logging_start_time, const char* logging_end_time ) {
    dest = json_objOpen( dest, "RegisterWriteLog" );             

    if ((channel != NULL) && (die != NULL) && (reg != NULL) && (logging_start_time != NULL) && (logging_end_time != NULL)){
        dest = json_int( dest, "channel", channel );
        dest = json_int( dest, "die", die );  
        dest = json_int( dest, "reg", reg );
        dest = json_LogMetadata(dest, "metadata", NULL, logging_start_time, logging_end_time);
    }
    else if(RegisterWriteLog != NULL){
        dest = json_int( dest, "channel", RegisterWriteLog->channel );
        dest = json_int( dest, "die", RegisterWriteLog->die );  
        dest = json_int( dest, "reg", RegisterWriteLog->reg );   
        dest = json_LogMetadata(dest, "metadata", &RegisterWriteLog->metadata, NULL, NULL);
    }
    else{
        
        printf("Error log Parameters to RegisterWriteLog");
    }
    
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

char* json_GarbageCollectionLog( char* dest) {
        dest = json_objOpen( dest, "GarbageCollectionLog" );   
        dest = json_objClose( dest );  
        char* p = json_end(dest);
        return dest - p;    
    }

char* json_BlockEraseLog( char* dest, struct BlockEraseLog const* BlockEraseLog, const int channel,const  int die, const int block, const char* logging_start_time, const char* logging_end_time ) {
    
    dest = json_objOpen( dest, "BlockEraseLog" );             

    if ((channel != NULL) && (die != NULL) && (block != NULL) && (logging_start_time != NULL) && (logging_end_time != NULL)){
        dest = json_int( dest, "channel", channel );
        dest = json_int( dest, "die", die );  
        dest = json_int( dest, "block", block );
        dest = json_LogMetadata(dest, "metadata", NULL, logging_start_time, logging_end_time);
    }
    else if(BlockEraseLog != NULL){
        dest = json_int( dest, "channel", BlockEraseLog->channel );
        dest = json_int( dest, "die", BlockEraseLog->die );  
        dest = json_int( dest, "block", BlockEraseLog->block );   
        dest = json_LogMetadata(dest, "metadata", &BlockEraseLog->metadata, NULL, NULL);
    }
    else{
        printf("%d, %d, %d, %s, %s\n", channel, die, block, logging_start_time, logging_end_time);
        printf("Error log Parameters to BlockEraseLog\n");
    }
    
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

char* json_ChannelSwitchToReadLog( char* dest, struct ChannelSwitchToReadLog const* ChannelSwitchToReadLog, const int channel, const char* logging_start_time, const char* logging_end_time ) {
    dest = json_objOpen( dest, "ChannelSwitchToReadLog" );             

    if ((channel != NULL) && (logging_start_time != NULL) && (logging_end_time != NULL)){
        dest = json_int( dest, "channel", channel );
        dest = json_LogMetadata(dest, "metadata", NULL, logging_start_time, logging_end_time);
    }
    else if(ChannelSwitchToReadLog != NULL){
        dest = json_int( dest, "channel", ChannelSwitchToReadLog->channel );
        dest = json_LogMetadata(dest, "metadata", &ChannelSwitchToReadLog->metadata, NULL, NULL);
    }
    else{
        
        printf("Error log Parameters to ChannelSwitchToReadLog");
    }
    
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

char* json_ChannelSwitchToWriteLog( char* dest, struct ChannelSwitchToWriteLog const* ChannelSwitchToWriteLog, const int channel, const char* logging_start_time, const char* logging_end_time ) {
    dest = json_objOpen( dest, "ChannelSwitchToWriteLog" );             

    if ((channel != NULL) && (logging_start_time != NULL) && (logging_end_time != NULL)){
        dest = json_int( dest, "channel", channel );
        dest = json_LogMetadata(dest, "metadata", NULL, logging_start_time, logging_end_time);
    }
    else if(ChannelSwitchToWriteLog != NULL){
        dest = json_int( dest, "channel", ChannelSwitchToWriteLog->channel ); 
        dest = json_LogMetadata(dest, "metadata", &ChannelSwitchToWriteLog->metadata, NULL, NULL);
    }
    else{
        
        printf("Error log Parameters to ChannelSwitchToWriteLog");
    }
    
    dest = json_objClose( dest );  
    char* p = json_end(dest);
    return dest - p;
}