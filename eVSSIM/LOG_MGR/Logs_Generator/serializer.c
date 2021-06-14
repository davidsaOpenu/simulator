#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#include "simulator_logs.c"

#define MAX_CH_NUM 10

int g_reg_status[MAX_CH_NUM]; // W/R
int g_channel_status[MAX_CH_NUM]; // W/R


char buff[2048];
int len;

#define State_Read 0 
#define State_Write 1
enum State {
    read = 0,
    write = 1,
};

bool progarmPage(FILE* fptr, int ch_num, int die, int reg, int block,int page){
  if(g_reg_status[ch_num] == State_Read){
    //dump REGISTER_WRITE 
    len = json_RegisterWriteLog( buff, NULL, ch_num, die, reg, time(NULL),time(NULL));
    fprintf(fptr, "{%s}\n", buff);
  }
  //dump PHYSICAL_CELL_PROGRAM
  len = json_PhysicalCellProgramLog( buff, NULL, ch_num, block, page, time(NULL),time(NULL) );
  fprintf(fptr, "{%s}\n", buff);
  return true;
}

bool readPage (FILE* fptr, int ch_num, int block,int page, int die){
  if(g_reg_status[ch_num] == State_Write){

    //dump CHANNEL_SWITCH_TO_READ
    len = json_ChannelSwitchToReadLog( buff, NULL, ch_num , time(NULL),time(NULL) );
        fprintf(fptr, "{%s}\n", buff);
    }
        
  //dump PHYSICAL_CELL_READ
  len = json_PhysicalCellReadLog( buff, NULL, ch_num, block, page, time(NULL),time(NULL) );
  fprintf(fptr, "{%s}\n", buff);
  return true;
}


bool erase_block(FILE* fptr, int ch_num, int die,  int block){
  len = json_BlockEraseLog( buff, NULL, ch_num, die, block, time(NULL),time(NULL) );
  fprintf(fptr, "{%s}\n", buff);
  return true;
}


bool logicalWrite(FILE* fptr, int ch_num, int block, int page, int die){
    if(g_reg_status[ch_num] == State_Read){
        //dump CHANNEL_SWITCH_TO_WRITE
        len = json_ChannelSwitchToWriteLog( buff, NULL, ch_num, time(NULL),time(NULL) );
        fprintf(fptr, "{%s}\n", buff);
    }
    // copy from a block to be erazed 
    for (int i = 0; i < rand() % 5; ++i)
    {
       //dump PHYSICAL_CELL_PROGRAM (from the block to be erazed to several block that contain free pages)
        len = json_PhysicalCellProgramLog( buff, NULL, ch_num, block, page, time(NULL),time(NULL) );
        fprintf(fptr, "{%s}\n", buff);
    }
  
    len = json_BlockEraseLog( buff, NULL, ch_num, die, block, time(NULL),time(NULL) );
    fprintf(fptr, "{%s}\n", buff);

    
    //dump PHYSICAL_CELL_PROGRAM / to the erazed block)
    len = json_PhysicalCellProgramLog( buff, NULL, ch_num, block, page, time(NULL),time(NULL) );
    fprintf(fptr, "{%s}\n", buff);
    
    //dump LOGICAL_CELL_PROGRAM
    len = json_LogicalCellProgramLog( buff, NULL, ch_num, block, page, time(NULL),time(NULL) );
    fprintf(fptr, "{%s}\n", buff);

    return true;
}


bool chSwich(FILE* fptr, int ch_num, int operation){
  if (g_channel_status[ch_num] != operation){
    //choose top dump CHANNEL_SWITCH_TO_WRITE or to dump CHANNEL_SWITCH_TO_READ
    if (operation == State_Read){
        len = json_ChannelSwitchToWriteLog( buff, NULL, ch_num, time(NULL),time(NULL) );
        fprintf(fptr, "{%s}\n", buff);
    }

    if (operation == State_Write){
        len = json_ChannelSwitchToReadLog( buff, NULL, ch_num , time(NULL),time(NULL) );
        fprintf(fptr, "{%s}\n", buff);
        }
    }

    return true;
}

bool garbageCollect(FILE* fptr){
    len = json_GarbageCollectionLog(buff);
    fprintf(fptr, "{%s}\n", buff);
}


int main (void)
{

    FILE *fptr;
    fptr = fopen("log.json","wb");
    /*
    Example for writing log from struct: 
    static struct PhysicalCellReadLog const PhysicalCellReadLog = {
                .channel  = 65,
                .block = 25,
                .page = 34,
                .metadata={
                  .logging_start_time="2019-12-20 11:48:12",
                  .logging_end_time="2019-12-20 12:48:12"
                }
    };
    len = json_PhysicalCellReadLog( buff, &PhysicalCellReadLog, NULL, NULL, NULL, NULL,NULL );
    */

    // Generate Random logs and write to file. 
    // Remember keep the format of ndjson http://ndjson.org/ - {} Will contain each object, /n will be a line seperator

        // fill the disk 

    for (int ch_num = 1; ch_num <= 4; ++ch_num)
    {
        //chSwich
        chSwich(fptr, 1, State_Write);
        for (int i = 0; i < 1; ++i)
        {
            // eraze to sumulate GC
            //25 eraze block (1)  
            progarmPage(fptr, ch_num, 100, 100, 100, 100);
            
            //25 dump GARBAGE_COLLECTION 
            garbageCollect(fptr);
        }
    }
    

    for (int ch_num = 1; ch_num <= 4; ++ch_num)
    {
            for (int i = 0; i < 1; ++i)
            {
                // eraze to sumulate GC
                //25 eraze block (1)  
                erase_block(fptr, ch_num, 100, 100);
                
                //25 dump GARBAGE_COLLECTION 
                garbageCollect(fptr);
            }
    }




    // reads

    for (int ch_num = 1; ch_num <= 4; ++ch_num)
    {
        //chSwich
        chSwich(fptr, 1, State_Read);
        for (int i = 0; i < 1; ++i)
        {
            // eraze to sumulate GC
            //25 eraze block (1)  
            readPage(fptr, ch_num, 100, 100, 100);
            
            //25 dump GARBAGE_COLLECTION 
            garbageCollect(fptr);
        }
    }
    


  // refill the disk 

    for (int ch_num = 1; ch_num <= 4; ++ch_num)
    {
        //chSwich
        chSwich(fptr, 1, State_Write);
        for (int i = 0; i < 1; ++i)
        {
            // eraze to sumulate GC
            //25 eraze block (1)  
            progarmPage(fptr, ch_num,100, 100, 100, 100);
            
            //25 dump GARBAGE_COLLECTION 
            garbageCollect(fptr);
        }
    }


    for (int ch_num = 1; ch_num <= 4; ++ch_num)
    {
        //chSwich
        chSwich(fptr, 1, State_Write);
        for (int i = 0; i < 1; ++i)
        {
            // eraze to sumulate GC
            //25 eraze block (1)  
            logicalWrite(fptr, ch_num, 100, 100, 100);
            
            //25 dump GARBAGE_COLLECTION 
            garbageCollect(fptr);
        }
    }
  

    // Example for converting struct to json 
    fclose(fptr);
    return EXIT_SUCCESS;




}
