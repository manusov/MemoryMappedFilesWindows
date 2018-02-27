/*
Memory mapped file io test. (C) IC Book Labs.
IPB/TPB/OPB communication sample.
See some details at: tmp1_linuxport.TXT , tmp2_fordisks.TXT.
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <windows.h>

//--- Title string ---
#if _WIN64
#define TITLE "Memory-mapped files benchmark for Windows 64.\n(C)2018 IC Book Labs. v0.06"
#else
#define TITLE "Memory-mapped files benchmark for Windows 32.\n(C)2018 IC Book Labs. v0.06"
#endif

//--- Defaults definitions ---
#define FILE_PATH   "myfile.bin"       // default file path and name
#define FILE_SIZE   1024*1024*1024     // default file size, bytes
#define WRITE_DELAY 100                // default delay from Start to Write in milliseconds, argument of Sleep()
#define READ_DELAY  100                // default delay from Write end to Read in milliseconds, argument of Sleep()
#define MEASURE_REPEATS 5              // default number of measurement repeats

//--- Limits definitions ---
#define FILE_SIZE_MIN  4096            // minimum file size 4096 bytes
#define FILE_SIZE_MAX  1536*1024*1024  // maximum file size 1.5 gigabytes
#define DELAY_MIN      0               // minimum delay value, 0 milliseconds
#define DELAY_MAX      100000          // maximum delay value, 100000 milliseconds = 100 seconds
#define REPEATS_MIN    0               // minimum number of measurement repeats
#define REPEATS_MAX    100             // maximum number of measurement repeats

//--- Timer constant ---
#define TIME_TO_SECONDS 0.0000001    // multiply by this to convert 100 nanosecond units to seconds

//--- Page walk constant ---
// #define PAGE_WALK_STEP 512        // step for cause swapping, page=4096 bytes but sector=512 bytes, make safe, actual only for READ
#define PAGE_WALK_STEP 4096

//--- Output tabulation options ---
#define IPB_TABS  18    // number of chars before "=" for tabulation, this used for start conditions (input parameters block)
#define OPB_TABS  8     // number of chars before "=" for tabulation, this used for results statistics (output/transit parm. block)

//--- Numeric data for storing command line options, with defaults assigned ---
static char    fileDefaultPath[] = FILE_PATH;   // constant string for references
static char*   filePath   = fileDefaultPath;    // pointer to file path string
static size_t  fileSize   = FILE_SIZE;          // file size, bytes
static int     writeDelay = WRITE_DELAY;        // delay from start to write, milliseconds
static int     readDelay  = READ_DELAY;         // delay from write end to read, milliseconds
static int     repeats = MEASURE_REPEATS;       // number of times to repeat test, for measurement precision

//--- File creation variables, parameters of CreateFile funcion  ---
static HANDLE fileHandle = NULL;                                                // file handle, result of CreateFile
static DWORD  fileAccess = GENERIC_READ | GENERIC_WRITE;                        // file access mode
static DWORD  fileShare  = NULL;                                                // file share mode, not used
static LPSECURITY_ATTRIBUTES fileSecurity = NULL;                               // security mode, not used
static DWORD fileCreate = CREATE_ALWAYS;                                        // file create mode, at start before write
static DWORD fileFlags  = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH |
                          FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN;   // file attributes, no buferring, write through
static HANDLE* fileTemplate = NULL;                                             // file template, not used
//--- File re-open variables changes ---
static DWORD fileOpen = OPEN_ALWAYS;                                            // file open mode, after write before read

//--- Mapping creation variables, parameters of CreateFileMapping function ---
static HANDLE mapHandle = NULL;                  // mapping handle, result of CreateMapping
static LPSECURITY_ATTRIBUTES mapSecurity = 0;    // mapping security mode, not used
static DWORD mapProtect = PAGE_READWRITE;        // mapping protection attributes
static DWORD mapSizeHigh = 0;                    // high 32 bits of mapping size
static DWORD mapSizeLow = 0;                     // low 32 bits of mapping size
static LPCTSTR mapName = NULL;                   // mapping name, not used

//--- Mapping address variables, parameters of MapViewOfFile function ---
static LPVOID mapPointer = NULL;                 // virtual address of mapping view, result of MapViewOfFile
static DWORD viewAccess = FILE_MAP_ALL_ACCESS;   // mapping view access mode
static DWORD viewOffsetHigh = 0;                 // high 32 bits of mapping size
static DWORD viewOffsetLow = 0;                  // low 32 bits of mapping size
// static SIZE_T viewSize = 0;

//--- Miscellaneous status ---
int status = 0;                                  // status for return by API functions

//--- Numeric data for benchmarks results statistics ---
static double readLog[REPEATS_MAX];    // array of read results, megabytes per second
static double writeLog[REPEATS_MAX];   // array of write results, megabytes per second
static int logCount = 0;               // number of actual log entries
static double resultMedian = 0.0;      // median speed, megabytes per second
static double resultAverage = 0.0;     // average speed, megabytes per second
static double resultMinimum = 0.0;     // minimum detected speed, megabytes per second
static double resultMaximum = 0.0;     // maximum detected speed, megabytes per second

//--- Data for timings and benchmarks ---
typedef union
    {
    FILETIME ft;           // return of GetSystemTimeAsFileTime
    long long lt;          // access as 64-bit value
    } UNITIME;
static UNITIME ut1, ut2;    // results of GetSystemTimeAsFileTime, at start and stop of measured interval

//--- Strings ---
static char sPath[]     = "path"     ,  // this for command line options names detect
			sSize[]     = "size"     ,
            sWdelay[]   = "wdelay"   ,
            sRdelay[]   = "rdelay"   ,
            sRepeats[]  = "repeats"  ,
            
            ssPath[]    = "file path"         ,    // this for start conditions visual
            ssSize[]    = "file size"         ,
            ssWdelay[]  = "write delay (ms)"  ,
            ssRdelay[]  = "read delay (ms)"   ,
            ssRepeats[] = "repeat times"      ,
            
            sMedian[]   = "Median"   ,             // this for result statistics median
            sAverage[]  = "Average"  ,
            sMinimum[]  = "Minimum"  ,
            sMaximum[]  = "Maximum"  ;

//--- Control block for command line parse, build IPB = Input Parameters Block ---
typedef enum
    { NOOPT, INTPARM, MEMPARM, SELPARM, STRPARM } OPTION_TYPES;
typedef struct
    {
    char* name;             // pointer to parm. name for recognition NAME=VALUE
    char** values;          // pointer to array of strings pointers, text opt.
    int n_values;           // number of strings for text option recognition
    void* data;             // pointer to updated option variable
    OPTION_TYPES routine;   // select handling method for this entry
    } OPTION_ENTRY;
    
//--- Entries for command line options, null-terminated list ---
static OPTION_ENTRY ipb_list[] =
    {
        { sPath    ,  NULL ,  0 ,  &filePath   ,  STRPARM },
		{ sSize    ,  NULL ,  0 ,  &fileSize   ,  MEMPARM },
		{ sWdelay  ,  NULL ,  0 ,  &writeDelay ,  INTPARM },
        { sRdelay  ,  NULL ,  0 ,  &readDelay  ,  INTPARM },
        { sRepeats ,  NULL ,  0 ,  &repeats    ,  INTPARM },
        { NULL     ,  NULL ,  0 ,  NULL        ,  NOOPT   }
    };

//--- Control block for start conditions parameters visual, bulid TPB = Transit Parameters Block ---
typedef enum
    { NOPRN, VDOUBLE, VINTEGER, MEMSIZE, SELECTOR, POINTER, HEX64, MHZ, STRNG } PRINT_TYPES;
typedef struct
    {
    char* name;             // pointer to parameter name for visual NAME=VALUE 
    char** values;          // pointer to array of strings pointers, text opt.
    void* data;             // pointer to visualized option variable
    PRINT_TYPES routine;    // select handling method for this entry
    } PRINT_ENTRY;

//--- Entries for print, null-terminated list ---
static PRINT_ENTRY tpb_list[] = 
    {
    	{ ssPath    ,  NULL ,  &filePath   ,  STRNG    },
		{ ssSize    ,  NULL ,  &fileSize   ,  MEMSIZE  },
		{ ssWdelay  ,  NULL ,  &writeDelay ,  VINTEGER },
        { ssRdelay  ,  NULL ,  &readDelay  ,  VINTEGER },
        { ssRepeats ,  NULL ,  &repeats    ,  VINTEGER },
        { NULL      ,  NULL ,  0           ,  NOPRN    }
    }; 

//--- Control block for result parameters visual, build OPB = Output Parameters Block ---
//--- Entries for print, null-terminated list ---
static PRINT_ENTRY opb_list[] = 
    {
        { sMedian     , NULL    , &resultMedian     , VDOUBLE  },
        { sAverage    , NULL    , &resultAverage    , VDOUBLE  },
        { sMinimum    , NULL    , &resultMinimum    , VDOUBLE  },
        { sMaximum    , NULL    , &resultMaximum    , VDOUBLE  },
        { NULL        , NULL    , 0                 , NOPRN    }
    };

//--- Conditional methods definition for 32 and 64-bit platforms ---
#if _WIN64
convert64( size_t value64, DWORD *low32, DWORD *high32 )
	{
	*low32 = value64 & 0xFFFFFFFFLL;
	*high32 = value64 >> 32;
	}
#else
convert64( size_t value64, DWORD *low32, DWORD *high32 )
	{
	*low32 = value64;
	*high32 = 0;
	}
#endif

//--- Helper method for print memory size: bytes/KB/MB/GB, to scratch string ---
// INPUT:   scratchPointer = pointer to destination string
//          memsize = memory size for visual, bytes
// OUTPUT:  number of chars write
//---
#define KILO 1024
#define MEGA 1024*1024
#define GIGA 1024*1024*1024
#define PRINT_LIMIT 20
int scratchMemorySize( char* scratchPointer, size_t memsize )
    {
    double xd = memsize;
    int nchars = 0;
    if ( memsize < KILO )
        {
        int xi = memsize;
        nchars = snprintf( scratchPointer, PRINT_LIMIT, "%d bytes", xi );
        }
    else if ( memsize < MEGA )
        {
        xd /= KILO;
        nchars = snprintf( scratchPointer, PRINT_LIMIT, "%.2lfK", xd );
        }
    else if ( memsize < GIGA )
        {
        xd /= MEGA;
        nchars = snprintf( scratchPointer, PRINT_LIMIT, "%.2lfM", xd );
        }
    else
        {
        xd /= GIGA;
        nchars = snprintf( scratchPointer, PRINT_LIMIT, "%.2lfG", xd );
        }
    return nchars;
    }

//--- Helper method for print memory size: bytes/KB/MB/GB, to console ---
// INPUT:   memsize = memory size for visual, bytes
// OUTPUT:  number of chars write
//---
int printMemorySize( size_t memsize )
    {
    double xd = memsize;
    int nchars = 0;
    if ( memsize < KILO )
        {
        int xi = memsize;
        nchars = printf( "%d bytes", xi );
        }
    else if ( memsize < MEGA )
        {
        xd /= KILO;
        nchars = printf( "%.2lfK", xd );
        }
    else if ( memsize < GIGA )
        {
        xd /= MEGA;
        nchars = printf( "%.2lfM", xd );
        }
    else
        {
        xd /= GIGA;
        nchars = printf( "%.2lfG", xd );
        }
    return nchars;
    }

//--- Helper method for print selected string from strings array ---
// INPUT: select = value for select string from strings array
//        names = strings array
//---
void printSelectedString( int select, char* names[] )
    {
    printf( "%s", names[select] );
    }

//--- Helper method for calculate median, average, minimum, maximum ---
// INPUT:   statArray[] = array of results
//          statCount = number of actual results in the array, can be smaller than array size
// OUTPUT:  update variables by input pointers:
//          statMedian, statAverage, statMin, statMax
//---
void calculateStatistics( double statArray[], int statCount,
                          double *statMedian, double *statAverage,
                          double *statMin, double *statMax )
    {
    double statSum = 0.0;
    double statTemp = 0.0;
    int flag = 0;
    int i = 0;
    //--- Minimum, Maximum, Average ---
    *statMin = statArray[0];
    *statMax = statArray[0];
    for ( i=0; i<statCount; i++ )
        {
        if ( *statMin > statArray[i] ) { *statMin = statArray[i]; }
        if ( *statMax < statArray[i] ) { *statMax = statArray[i]; }
        statSum += statArray[i];
        }
    *statAverage = statSum / statCount;
    //--- Median, first ordering ---
    flag = 1;
    while ( flag == 1 )
        {
        flag = 0;
        for ( i=0; i<(statCount-1); i++ )
            {
            if ( statArray[i] > statArray[i+1] )
                {
                statTemp = statArray[i];
                statArray[i] = statArray[i+1];
                statArray[i+1] = statTemp;
                flag = 1;
                }
            }
        }
    if ( ( statCount % 2 ) == 0 )
        {  // median if array length EVEN, average of middle pair
        i = statCount / 2;
        *statMedian = ( statArray[i-1] + statArray[i] ) / 2.0;
        }
    else
        {  // median if array length ODD, middle element
        i = statCount/2;
        *statMedian = statArray[i];
        }
    }

//--- Handler for Receive console input (command line, text file or GUI shell) data to IPB ---
// IPB = Input Parameters Block
// INPUT:   pCount = number of command line parameters 
//          pStrings = array of command line strings NAME=VALUE, program name included first
//          parse_control = control array for parsing formal description
// OUTPUT:  status, 0=parsed OK, otherwise parsing error, messages output to console
//---
int handlerInput( int pCount, char** pStrings, OPTION_ENTRY parse_control[] )
{
//--- Accept command line options ---
int i=0, j=0, k=0, k1=0, k2=0;  // miscellaneous counters and variables
int recognized = 0;             // result of strings comparision, 0=match 
OPTION_TYPES t = NOOPT;         // enumeration of parameters types for accept
char* pAll = NULL;              // pointer to option full string NAME=VALUE
char* pName = NULL;             // pointer to sub-string NAME
char* pValue = NULL;            // pointer to sub-string VALUE
char* pPattern = NULL;          // pointer to compared pattern string
char** pPatterns = NULL;        // pointer to pointer to pattern strings
int* pInt = NULL;               // pointer to integer (32b) for variable store
// long long* pLong = NULL;     // pointer to long (64b) for variable store
// long long k64;               // transit variable for memory block size
size_t* pSize = NULL;
size_t kSize = 0;
// bug fix 32/64
char c = 0;                     // transit storage for char
#define SMIN 3                  // minimum option string length, example a=b
#define SMAX 81                 // maximum option string length
char cmdName[SMAX];             // extracted NAME of option string
char cmdValue[SMAX];            // extracted VALUE of option string

for ( i=1; i<pCount; i++ )      // cycle for command line options
    {
    // initializing for parsing current string
    // because element [0] is application name, starts from [1]
    pAll = pStrings[i];
    for ( j=0; j<SMAX; j++ )  // clear buffers
        {
        cmdName[j]=0;
        cmdValue[j]=0;
        }
    // check option sub-string length
    k = strlen(pAll);                   // k = length of one option sub-string
    if ( k<SMIN )
        {
        printf( "ERROR, OPTION TOO SHORT: %s\n", pAll );
        return 1;
        }
    if ( k>SMAX )
        {
        printf( "ERROR, OPTION TOO LONG: %s\n", pAll );
        return 1;
        }
    // extract option name and option value substrings
    pName = cmdName;
    pValue = cmdValue;
    strcpy( pName, pAll );           // store option sub-string to pName
    strtok( pName, "=" );            // pName = pointer to fragment before "="
	pValue = strtok( NULL, " " );    // pValue = pointer to fragment after "="
	
/* DEBUG	
	printf("\npAll   = %s\n", pAll   );
	printf("pName  = %s\n", pName  );
	printf("pValue = %s\n\n", pValue );
	//pValue = "aaaa";
	pValue = "TEST_TEST";
DEBUG */	
	
	// check option name and option value substrings
    k1 = 0;
    k2 = 0;
    if ( pName  != NULL ) { k1 = strlen( pName );  }
    if ( pValue != NULL ) { k2 = strlen( pValue ); }
    if ( ( k1==0 )||( k2==0 ) )
        {
        printf( "ERROR, OPTION INVALID: %s\n", pAll );
        return 1;
        }
    // detect option by comparision from list, cycle for supported options
    for ( j=0; parse_control[j].name!=NULL; j++ )
        {
        pPattern = parse_control[j].name;
        recognized = strcmp ( pName, pPattern );
        if ( recognized==0 )
            {
            // option-type specific handling, run if name match
            t = parse_control[j].routine;
            switch(t)
                {
                case INTPARM:  // support integer parameters
                    {
                    k1 = strlen( pValue );
                    for ( k=0; k<k1; k++ )
                        {
                        if ( isdigit( pValue[k] ) == 0 )
                            {
                            printf( "ERROR, NOT A NUMBER: %s\n", pValue );
                            return 1;
                            }
                        }
                    k = atoi( pValue );   // convert string to integer
                    pInt = (int *) parse_control[j].data;
                    *pInt = k;
                    break;
                    }
                case MEMPARM:  // support memory block size parameters
                    {
                    k1 = 0;
                    k2 = strlen( pValue );
                    c = pValue[k2-1];
                    if ( isdigit(c) != 0 )
                        {
                        k1 = 1;             // no units kilo, mega, giga
                        }
                    else if ( c == 'K' )    // K means kilobytes
                        {
                        k2--;               // last char not a digit K/M/G
                        k1 = 1024;
                        }
                    else if ( c == 'M' )    // M means megabytes
                        {
                        k2--;
                        k1 = 1024*1024;
                        }
                    else if ( c == 'G' )    // G means gigabytes
                        {
                        k2--;
                        k1 = 1024*1024*1024;
                        }
                    for ( k=0; k<k2; k++ )
                        {
                        if ( isdigit( pValue[k] ) == 0 )
                            {
                            k1 = 0;
                            }
                        }
                    if ( k1==0 )
                        {
                        printf( "ERROR, NOT A BLOCK SIZE: %s\n", pValue );
                        return 1;
                        }
                    k = atoi( pValue );   // convert string to integer
                    // k64 = k;
                    // k64 *= k1;
                    // pLong = (long long int *) parse_control[j].data;
                    // *pLong = k64;
                    kSize = k;
                    kSize *= k1;
                    pSize = (size_t *) parse_control[j].data;
                    *pSize = kSize;
                    // bug fix 32/64
                    break;
                    }
                case SELPARM:    // support parameters selected from text names
                    {
                    k1 = parse_control[j].n_values;
                    k2 = 0;
                    pPatterns = parse_control[j].values;
                    for ( k=0; k<k1; k++ )
                        {
                        pPattern = pPatterns[k];
                        k2 = strcmp ( pValue, pPattern );
                        if ( k2==0 )
                            {
                            pInt = (int *) parse_control[j].data;
                            *pInt = k;
                            break;
                            }
                        }
                    if ( k2 != 0 )
                        {
                        printf( "ERROR, VALUE INVALID: %s\n", pAll );
                        return 1;
                        }
                    break;
                    }
                case STRPARM:    // support parameter as text string
                    {
					pPatterns = (char **) parse_control[j].data;
                    // *pPatterns = pValue;
                    *pPatterns = pStrings[i] + k1 + 1;          // skip string before "=" and "=" char
                    // fix bug with local variables destroyed
                    break;
                    }
                }
            break;
            }
        }
    // check option name recognized or not
    if ( recognized != 0 )
        {
        printf( "ERROR, OPTION NOT RECOGNIZED: %s\n", pName );
        return 1;
        }
    }
return 0;
}

//--- Handler for Transmit TPB to console output (text screen, text file or GUI shell) ---
// TPB = Transit Parameters Block
//--- Handler for Transmit OPB to console output (text screen, text file or GUI shell) ---
// OPB = Output Parameters Block
// INPUT:   print_control = control array for output formal description
//          tabSize = number of tabulations before "="
// same handler for TPB and OPB: handlerOutput().
//---
void handlerOutput( PRINT_ENTRY print_control[] , int tabSize )
{
int i=0, j=0, k=0, k1=0, k2=0;   // miscellaneous counters and variables
PRINT_TYPES m = NOPRN;           // enumeration of parameters types for print
long long unsigned int n = 0;         // value for block size
long long unsigned int* np = NULL;    // pointer to block size value
double d = 0.0;                  // transit variable
double* dp = NULL;               // transit pointer to double
unsigned long long* lp = NULL;   // transit pointer to long long
int*  kp = NULL;                 // pointer to integer
char* cp = NULL;                 // pointer to char
char** ccp = NULL;               // pointer to array of pointers to strings
size_t* sizep = 0;               // pointer to block size variable
size_t size = 0;                 // block size variable
// cycle for print parameters strings
for ( i=0; print_control[i].name!=NULL; i++ )
    {
    k = tabSize - printf( "%s", print_control[i].name );
    for ( j=0; j<k; j++ )
        {
        printf(" ");
        }
    printf("= ");
    m = print_control[i].routine;
    switch(m)
        {
        case VDOUBLE:   // double parameter
            {
            dp = (double *) print_control[i].data;
            d = *dp;
            printf( "%.3f", d );
            break;
            }
        case VINTEGER:  // integer parameter
            {
            kp = (int *) print_control[i].data;
            k = *kp;
            printf( "%d", k );
            break;
            }
        case MEMSIZE:  // memory block size parameter
            {
            sizep = (size_t *) print_control[i].data;
            size = *sizep;
            printMemorySize( size );
            break;
            }
        case SELECTOR:  // pool of text names parameter
            {
            kp = (int *) print_control[i].data;
            k = *kp;
            ccp = print_control[i].values;
            printSelectedString( k, ccp );
            break;
            }
        case POINTER:  // memory pointer parameter
            {
            ccp = (char **) print_control[i].data;
            cp = *ccp;
            printf( "%ph", cp );
            break;
            }
        case HEX64:  // 64-bit hex number parameter
            {
            np = (long long unsigned int *) print_control[i].data;
            n = *np;
            printf( "0x%08llX", n );
            break;
            }
        case MHZ:  // frequency in MHz parameter
            {
            lp = (long long unsigned int *) print_control[i].data;
            d = *lp;  // with convert from unsigned long long to double
            d /= 1000000.0;
            printf( "%.1f MHz", d );
            break;
            }
        case STRNG:  // parameter as text string
            {
            ccp = (char **) print_control[i].data;
            cp = *ccp;
            printf( "%s", cp );
            break;
            }
        }
    printf("\n");
    }
}

//--- Handler for output current string at test progress ---
// INPUT:  char* stepName = name of step
//         int   stepNumber = number of step (pass)
//         double statArray[] = statistic array, stepNumber entries used
//---
void handlerProgress( char stepName[], int stepNumber, double statArray[] )
    {
    	
    double currentMBPS = statArray[stepNumber];
    
    calculateStatistics( statArray, stepNumber + 1,
                        &resultMedian, &resultAverage,
                        &resultMinimum, &resultMaximum );
/*
    int i = 0;
    int j = 8;
    j -= printf ( " %d", stepNumber+1 );
    for ( i=0; i<j; i++ ) { printf( " " ); }
    
    j = 11;
    j -= printf ( "%s", stepName );
    for ( i=0; i<j; i++ ) { printf( " " ); }
    
    j = 11;
    j -= printf( "%.3f", statArray[stepNumber] );
    for ( i=0; i<j; i++ ) { printf( " " ); }
    
    j = 11;
    j -= printf( "%.3f", resultMedian );
    for ( i=0; i<j; i++ ) { printf( " " ); }
    
    j = 11;
    j -= printf( "%.3f", resultAverage );
    for ( i=0; i<j; i++ ) { printf( " " ); }
    
    j = 11;
    j -= printf( "%.3f", resultMinimum );
    for ( i=0; i<j; i++ ) { printf( " " ); }
    
    j = 11;
    j -= printf( "%.3f", resultMaximum );
    for ( i=0; i<j; i++ ) { printf( " " ); }
    
    printf ( "\n" );
*/

	printf( " %-6d%-11s%8.3f%11.3f%11.3f%11.3f%11.3f\n",
	        stepNumber+1,
	        stepName,
	        // statArray[stepNumber],
	        currentMBPS,
	        //
	        resultMedian,
	        resultAverage,
	        resultMinimum,
	        resultMaximum
	      );
	
	}



//---------- Application entry point -------------------------------------------

int main( int argc, char** argv )
{
//--- Start message ---
printf( "\n%s\n\n", TITLE );

//--- Parse command line ---
if ( handlerInput( argc, argv, ipb_list ) != 0 ) return 1;

//--- Title string for test conditions ---
printf( "Start conditions:\n" );

//--- Print transit (config) parameters ---
handlerOutput( tpb_list, IPB_TABS );

//--- Check start parameters validity and compatibility ---
if ( ( fileSize < FILE_SIZE_MIN ) | ( fileSize > FILE_SIZE_MAX ) )
    {
    printf("\nBAD PARAMETER: file size must be from " );
    printMemorySize( FILE_SIZE_MIN );
    printf(" to ");
    printMemorySize( FILE_SIZE_MAX );
    printf( "\n" );
    return 1;
    }
if ( ( writeDelay < DELAY_MIN ) | ( writeDelay > DELAY_MAX ) )
    {
	printf("\nBAD PARAMETER: Write delay must be from %d to %d milliseconds\n", DELAY_MIN, DELAY_MAX );
    return 1;
    }
if ( ( readDelay < DELAY_MIN ) | ( readDelay > DELAY_MAX ) )
    {
    printf("\nBAD PARAMETER: Read delay must be from %d to %d milliseconds\n", DELAY_MIN, DELAY_MAX );
    return 1;
    }
if ( ( repeats < REPEATS_MIN ) | ( repeats > REPEATS_MAX ) )
    {
    printf("\nBAD PARAMETER: Repeats must be from %d to %d times\n", REPEATS_MIN, REPEATS_MAX );
    return 1;
    }

//--- Wait for key (Y/N) with list of start parameters ---
printf("\nStart? (Y/N)" );
int key = 0;
key = getchar();
key = tolower(key);
if ( key != 'y' )
    {
    printf( "Test skipped.\n" );
    return 3;
    }
printf( "\n" );

//--- Blank log arrays ---
int rep = repeats;
for ( rep=0; rep<10; rep++ )
	{
	readLog[rep] = 0.0;
	writeLog[rep] = 0.0;
	}

//--- Cycle for measurement repeats ---
// printf("Benchmarking...\n");
//--- Cycle for measurement repeats ---
printf( "\nStart benchmarking.\n" );
printf( "Pass | Operation | MBPS     | Median   | Average  | Minimum  | Maximum\n" );
printf( "-------------------------------------------------------------------------\n\n" );

for ( rep=0; rep<repeats; rep++ )
	{

	//--- Create file ---
	fileHandle = CreateFile( filePath, fileAccess, fileShare, fileSecurity, fileCreate, fileFlags, fileTemplate );
	if ( fileHandle == NULL )
		{
		printf ( "Error create file\n" );
		return 2;
		}
	else
		{
		// printf( "File created...\n" );  // make silent version
		}

	//--- WRITE PHASE: Create mapping object for file ---
	// mapSizeLow = fileSize & 0xFFFFFFFFLL;
	// mapSizeHigh = fileSize >> 32;
	convert64 ( fileSize, &mapSizeLow, &mapSizeHigh );
	// bug 32/64 fixed
	mapHandle = CreateFileMapping( fileHandle, mapSecurity, mapProtect, mapSizeHigh, mapSizeLow, mapName );
	if ( mapHandle == NULL )
		{
		printf ( "Error create mapping\n" );
		return 2;
		}
	else
		{
		// printf( "Mapping created...\n" );  // make silent version
		}

	//--- WRITE PHASE: Mapping created object to address space ---
	mapPointer = MapViewOfFile( mapHandle, viewAccess, viewOffsetHigh, viewOffsetLow, fileSize );
	if ( mapPointer == NULL )
		{
		printf ( "Error create view\n" );
		return 2;
		}
	else
		{
		// printf( "View at virtual address %ph created...\n", mapPointer );  // make silent version
		}

	//--- WRITE PHASE: Fill buffer for write data to file without page faults in the measure time ---
	// printf("Write buffer...\n");  // make silent version
	char setData = '1';
	// memset ( mapPointer, setData, mapSizeLow );  // thsi limits buffer size to 32-bit value
	memset ( mapPointer, setData, fileSize );
	// bug with 32/64 fixed

	//--- WRITE PHASE: Flush modified data to file, means write operation, with time measurement ---
	// printf("Write delay...\n");  // make silent version
	Sleep(writeDelay);
	// printf("Write file...\n");  // make silent version
	//--- start timings ---
	GetSystemTimeAsFileTime( &ut1.ft );
	status = FlushViewOfFile( mapPointer, fileSize );
	GetSystemTimeAsFileTime( &ut2.ft );
	//--- end timings ---
	if ( status == 0 )
		{
		printf ( "Error flush file\n" );
		return 2;
		}
	else
		{
		double megabytes = fileSize;
		megabytes /= 1048576.0;               // convert from bytes to megabytes
		double seconds = ut2.lt - ut1.lt;
		seconds *= TIME_TO_SECONDS;           // convert from 100ns-units to seconds
		double mbps = megabytes / seconds;
		// printf( "Flush file OK, write speed = %.3f MBPS\n" , mbps );    // make silent version
		//
		// printf( "Pass %d write MBPS = %.3f\n", rep+1, mbps );
		//
		writeLog[rep] = mbps;
		//
		handlerProgress( "write", rep, writeLog );
		//
		}

	//--- WRITE PHASE: Close mapping object ---
	status = CloseHandle( mapHandle );
	if ( status == 0 )
		{
		printf ( "Error close mapping\n" );
		return 2;
		}

	//--- WRITE PHASE: Close file ---
	status = CloseHandle( fileHandle );
	if ( status == 0 )
		{
		printf ( "Error close file\n" );
		return 2;
		}

	//--- WRITE PHASE: Unmap view of file ---
	// Note file not deleted for next operations.
	status = UnmapViewOfFile( mapPointer );
	if ( status == 0 )
		{
		printf ( "Error unmap file\n" );
		return 2;
		}

	//--- READ PHASE: Re-Open file ---
	fileHandle = CreateFile( filePath, fileAccess, fileShare, fileSecurity, fileOpen, fileFlags, fileTemplate );
	if ( fileHandle == NULL )
		{
		printf ( "Error re-open file\n" );
		return 2;
		}
	else
		{
		// printf( "File re-opened...\n" );    // make silent version
		}
	
	//--- READ PHASE: Re-Create mapping object for file ---
	// mapSizeLow = fileSize & 0xFFFFFFFFLL;
	// mapSizeHigh = fileSize >> 32;
	convert64 ( fileSize, &mapSizeLow, &mapSizeHigh );
	// bug 32/64 fixed
	mapHandle = CreateFileMapping( fileHandle, mapSecurity, mapProtect, mapSizeHigh, mapSizeLow, mapName );
	if ( mapHandle == NULL )
		{
		printf ( "Error re-create mapping\n" );
		return 2;
		}
	else
		{
		// printf( "Mapping re-created...\n" );    // make silent version
		}

	//--- READ PHASE: Mapping created object to address space ---
	mapPointer = MapViewOfFile( mapHandle, viewAccess, viewOffsetHigh, viewOffsetLow, fileSize );
	if ( mapPointer == NULL )
		{
		printf ( "Error re-create view\n" );
		return 2;
		}
	else
		{
		// printf( "View at virtual address %ph re-created...\n", mapPointer );    // make silent version
		}

	//--- READ PHASE:  page walk means swap operation, with time measurement ---
	// printf("Read delay...\n");    // make silent version
	Sleep(readDelay);
	// printf("Read file by page walk...\n");     // make silent version

	int walkSize = PAGE_WALK_STEP;
	char walkData = 0;
	char* readPointer = (char *) mapPointer;
	int readCount = 0;
	int readLimit = mapSizeLow / walkSize;

	//--- start timings ---
	GetSystemTimeAsFileTime( &ut1.ft );
	for ( readCount=0; readCount<readLimit; readCount++ )
		{
		walkData = *readPointer;   // this causes swapping or DAX map for adressed pages
		readPointer += walkSize;
		}
	GetSystemTimeAsFileTime( &ut2.ft );
	//--- end timings ---
	if ( status == 0 )  // status not updated above, use previous (bug)
		{
		printf ( "Error read file\n" );
		return 2;
		}
	else
		{
		double megabytes = fileSize;
		megabytes /= 1048576.0;               // convert from bytes to megabytes
		double seconds = ut2.lt - ut1.lt;
		seconds *= TIME_TO_SECONDS;           // convert from 100ns-units to seconds
		double mbps = megabytes / seconds;
		// printf( "Read page walk OK, read speed = %.3f MBPS\n\n" , mbps );     // make silent version
		//
		// printf( "Pass %d read  MBPS = %.3f\n", rep+1, mbps );
		//
		readLog[rep] = mbps;
		//
		handlerProgress( "read", rep, readLog );
		//
		}

	//--- READ PHASE: Close mapping object ---
	status = CloseHandle( mapHandle );
	if ( status == 0 )
		{
		printf ( "Error close mapping\n" );
		return 2;
		}

	//--- READ PHASE: Close file ---
	status = CloseHandle( fileHandle );
	if ( status == 0 )
		{
		printf ( "Error close file\n" );
		return 2;
		}

	//--- READ PHASE: Unmap view of file ---
	// Note file not deleted for next operations.
	status = UnmapViewOfFile( mapPointer );
	if ( status == 0 )
		{
		printf ( "Error unmap file\n" );
		return 2;
		}

	//--- READ PHASE: Delete file ---
	status = DeleteFile( filePath );
	if ( status == 0 )
		{
		printf ( "Error delete file\n" );
		return 2;
		}
	
	}
	
printf( "\n-------------------------------------------------------------------------\n" );

//--- Print output parameters, read results ---
printf( "\nWrite statistics (MBPS):\n" );
calculateStatistics(  writeLog, repeats,
                     &resultMedian, &resultAverage,
                     &resultMinimum, &resultMaximum );
handlerOutput( opb_list, OPB_TABS );

//--- Print output parameters, read results ---
printf( "\nRead statistics (MBPS):\n" );
calculateStatistics(  readLog, repeats,
                     &resultMedian, &resultAverage,
                     &resultMinimum, &resultMaximum );
handlerOutput( opb_list, OPB_TABS );

//--- Exit ---
printf( "\nDone.\n" );
return 0;
}

