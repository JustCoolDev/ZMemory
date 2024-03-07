// These following lines have been fully wrote by JUSTCOOL GAMER NOT AC3SS0R
#include <unistd.h>
#include <cstdio>
#include <getopt.h>
#include "include/ZMemory.h"

using namespace ZMemory;

int main(int argc, char* argv[]) {
    pid_t pid = 0;
    long long base = 0;
    bool restore = false;
    int option;
    char* processName = nullptr;
    char* libName = nullptr;
    char* hexValue = nullptr;
    long long memoryOffset = 0x0;  // Default memory offset

    while ((option = getopt(argc, argv, "p:l:h:o:r:")) != -1) {
        switch (option) {
            case 'p':
                processName = optarg;
                break;
            case 'l':
                libName = optarg;
                break;
            case 'h':
                hexValue = optarg;
                break;
            case 'o':
                sscanf(optarg, "%llx", &memoryOffset);
                break;
            case 'r':
            	restore = true;
            	break;
            default:
                printf("Usage: %s -p process_name -l library_name -h hex_value -o memory_offset\n", argv[0]);
                return 1;
        }
    }

    if (!processName || !libName || !hexValue) {
        printf("Missing required arguments.\n");
        printf("Usage: %s -p process_name -l library_name -h hex_value -o memory_offset\n", argv[0]);
        return 1;
    }

  //  printf("Waiting for target process...\n");

    while (!(pid = find_pid(processName))) {
        sleep(1);
    }
    
	while (!(base = find_library_base(pid, libName))) {
        sleep(1);
    }
  	
    ZPatch patch = ZPatch(pid, base + memoryOffset, hexValue);
	if(!restore){
    	if (patch.toggle())
        	printf("Memory patched successfully\n");
    	else
        	printf("Failed to patch PID %d\n", pid);
	}else{
		patch.orig_datas();
		printf("%s", patch.Export_orig_data);
	}
    return 0;
}
