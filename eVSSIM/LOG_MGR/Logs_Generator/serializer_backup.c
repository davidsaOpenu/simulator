#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include "simulator_logs.c"

#define MAX_CH_NUM 10

int g_reg_status[MAX_CH_NUM]; // W/R
int g_channel_status[MAX_CH_NUM]; // W/R

enum State {
    read = 0,
    write = 1,
};

/*
bool progarmPage(int ch_num){
  if(g_reg_status[CH_NUM] == State.read)
    dump REGISTER_WRITE 
  dump PHYSICAL_CELL_PROGRAM
  return true;
}

bool readPage (int ch_num){
  if(g_reg_status[CH_NUM] == W)
    dump CHANNEL_SWITCH_TO_READ
  dump PHYSICAL_CELL_READ
  return true;
}


bool erase_block(in ch_num){
  dump BLOCK_ERASE
  return true;
}


bool logicalWrite(int ch_num){
    if(g_reg_status[CH_NUM] == R)
     dump CHANNEL_SWITCH_TO_WRITE
    // copy from a block to be erazed 
    for r = rand (5)
       dump PHYSICAL_CELL_PROGRAM (from the block to be erazed to several block that contain free pages)
    
    dump BLOCK_ERASE // the blck that pages we copied where located in 

    
    dump PHYSICAL_CELL_PROGRAM / to the erazed block)
    
    dump LOGICAL_CELL_PROGRAM
    return true;
}


bool chSwich(int ch_num, int operation){
  if g_channel_status[ch_num] != operation)
     choose top dump CHANNEL_SWITCH_TO_WRITE or to dump CHANNEL_SWITCH_TO_READ
    
    return true;
}

*/
int main (void)
{

         static struct PhysicalCellProgramLog const PhysicalCellProgramLog = {
                .channel  = 65,
                .block = 25,
                .page = 34,
                .metadata={
                  .logging_start_time="2019-12-20 08:48:12",
                  .logging_end_time="2019-12-20 08:48:12"
                }
    };
         static struct LogicalCellProgramLog const LogicalCellProgramLog = {
                .channel  = 65,
                .block = 25,
                .page = 34,
                .metadata={
                  .logging_start_time="2019-12-20 08:48:12",
                  .logging_end_time="2019-12-20 08:48:12"
                }
    };
         static struct RegisterReadLog const RegisterReadLog = {
                .channel  = 65,
                .die = 25,
                .reg = 34,
                .metadata={
                  .logging_start_time="2019-12-20 08:48:12",
                  .logging_end_time="2019-12-20 08:48:12"
                }
    };
         static struct RegisterWriteLog const RegisterWriteLog = {
                .channel  = 65,
                .die = 25,
                .reg = 34,
                .metadata={
                  .logging_start_time="2019-12-20 08:48:12",
                  .logging_end_time="2019-12-20 08:48:12"
                }
    };
         static struct BlockEraseLog const BlockEraseLog = {
                .channel  = 65,
                .die = 25,
                .block = 34,
                .metadata={
                  .logging_start_time="2019-12-20 08:48:12",
                  .logging_end_time="2019-12-20 08:48:12"
                }
    };
         static struct ChannelSwitchToReadLog const ChannelSwitchToReadLog = {
                .channel  = 65,
                .metadata={
                  .logging_start_time="2019-12-20 08:48:12",
                  .logging_end_time="2019-12-20 08:48:12"
                }
    };

             static struct ChannelSwitchToWriteLog const ChannelSwitchToWriteLog = {
                .channel  = 65,
                .metadata={
                  .logging_start_time="2019-12-20 08:48:12",
                  .logging_end_time="2019-12-20 08:48:12"
                }
    };
    
    FILE *fptr;

    fptr = fopen("log.json","wb");

    char buff[2048];
    int len;

    // Generate Random logs and write to file. 
    // Remember keep the format of ndjson http://ndjson.org/ - {} Will contain each object, /n will be a line seperator
    for (int i = 0; i < 1; ++i)
    {
            
    
        len = json_PhysicalCellReadLog( buff, 1, 2, 3, "2019-12-20 08:48:12","2019-12-20 10:48:12" );
         /*
        len = json_PhysicalCellReadLog( buff, NULL, 1, 2, 3, "2019-12-20 08:48:12","2019-12-20 10:48:12" );
        fprintf(fptr, "{%s}\n", buff);
        len = json_PhysicalCellProgramLog( buff, NULL, 1, 2, 3, "2019-12-20 08:48:12","2019-12-20 10:48:12" );
        fprintf(fptr, "{%s}\n", buff);
        
        len = json_LogicalCellProgramLog( buff, NULL, 1, 2, 3, "2019-12-20 08:48:12","2019-12-20 10:48:12" );
        fprintf(fptr, "{%s}\n", buff);
        
        len = json_RegisterReadLog( buff, NULL, 1, 2, 3, "2019-12-20 08:48:12","2019-12-20 10:48:12" );
        fprintf(fptr, "{%s}\n", buff);
        
        len = json_BlockEraseLog( buff, NULL, 1, 2, 3, "2019-12-20 08:48:12","2019-12-20 10:48:12" );
        fprintf(fptr, "{%s}\n", buff);

        len = json_ChannelSwitchToReadLog( buff, NULL, 1 , "2019-12-20 08:48:12","2019-12-20 10:48:12" );
        fprintf(fptr, "{%s}\n", buff);
        
        len = json_ChannelSwitchToWriteLog( buff, NULL, 1, "2019-12-20 08:48:12","2019-12-20 10:48:12" );
        fprintf(fptr, "{%s}\n", buff);
        */
        fprintf(fptr, "{%s}\n", buff);

        len = json_PhysicalCellProgramLog( buff, &PhysicalCellProgramLog );
        fprintf(fptr, "{%s}\n", buff);
        
        len = json_LogicalCellProgramLog( buff, &LogicalCellProgramLog );
        fprintf(fptr, "{%s}\n", buff);
        
        len = json_RegisterReadLog( buff, &RegisterReadLog );
        fprintf(fptr, "{%s}\n", buff);

        len = json_RegisterWriteLog( buff, &RegisterWriteLog );
        fprintf(fptr, "{%s}\n", buff);
        
        len = json_BlockEraseLog( buff, &BlockEraseLog );
        fprintf(fptr, "{%s}\n", buff);

        len = json_ChannelSwitchToReadLog( buff, &ChannelSwitchToReadLog );
        fprintf(fptr, "{%s}\n", buff);
        
        len = json_ChannelSwitchToWriteLog( buff, &ChannelSwitchToWriteLog );
        fprintf(fptr, "{%s}\n", buff);
    }

    fclose(fptr);
    return EXIT_SUCCESS;




}