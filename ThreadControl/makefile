
RM := rm -rf

USER_OBJS :=

LIBS := -ldl -lpthread -lrt

OBJ_SRCS := 
ASM_SRCS := 
C_SRCS := 
O_SRCS := 
S_UPPER_SRCS := 
EXECUTABLES := 
OBJS := 
C_DEPS := 

# Every subdirectory with source files must be described here
SUBDIRS := \

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
ThreadControl.c 

OBJS += \
ThreadControl.o 

C_DEPS += \
ThreadControl.d 


# Each subdirectory must supply rules for building sources it contributes
src/%.o: ../src/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: ThreadControl

# Tool invocations
ThreadControl: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GCC C Linker'
	gcc  -o "ThreadControl" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(EXECUTABLES)$(OBJS)$(C_DEPS) ThreadControl
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets
