// gcc lightlock.c -framework IOKit -framework ApplicationServices -o lightlock

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <unistd.h>
#include <mach/mach_time.h>

#define WINDOWSIZE 10 // количество накопляемых значений датчика
#define GESTURES "+vvv000-^^^" // описание жеста
#define UPDATEINTERVAL .01 // интервал считывания значений датчика
#define GESTDELAY 1400 // максимальная задержка до следующего жеста
#define GESTNUM 2 // число жестов - 1
#define GESTJOINDIFF 20 // задержка между срабатываниями внутри жеста

const kGetSensorReadingID = 0;
int makeReaction(uint64_t current);
static io_connect_t port = 0;
static double conversion_factor;

// Переключение Мака в режим блокировки
void macLock () {
    CGSCreateLoginSession(NULL);
}

// Считаем количество жестов
void delayedReaction() {
    static uint64_t start = 0;
    static int countevents = 0;

    uint64_t now = mach_absolute_time();

    if (start) {
        double duration_ms = (now - start) * conversion_factor / 1000000;

        if (duration_ms > GESTDELAY) {
            countevents = start = 0;
        } else {
            if (duration_ms > GESTJOINDIFF && ++countevents >= GESTNUM) {
                countevents = start = 0;

                macLock();
                sleep(3); // даём экрану логина отработать нормально
            }
         }
    }
    start = now;
}

// Считывание датчика освещёности
void updateTimerCallBack(CFRunLoopTimerRef timer, void *info) {
    IOItemCount osize = 2;
    uint64_t values[osize];

    kern_return_t ret = IOConnectCallMethod(port, kGetSensorReadingID,
    NULL, 0, NULL, 0, values, &osize, NULL, 0);

    if (ret == KERN_SUCCESS) {
        if (checkGesture(values[0])) {
            delayedReaction();
        }
    }
    else if (ret != kIOReturnBusy) {
      mach_error("IOConnectCallMethod: ", ret);
      exit(-3);
    }   
}

int main() {
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    conversion_factor = (timebase.numer / timebase.denom);

    io_service_t service = IOServiceGetMatchingService(
        kIOMasterPortDefault, IOServiceMatching("AppleLMUController")
    );

    if (service == kIOReturnNoDevice) {
        fprintf(stderr, "Cannot find Ambient Light Sensor\n");
        exit(-1);
    }

    kern_return_t ret = IOServiceOpen(service, mach_task_self(), 0, &port);
    IOObjectRelease(service);

    if (ret != KERN_SUCCESS) {
        mach_error("IOServiceOpen:", ret);
        exit(-2);
    }

    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + UPDATEINTERVAL,
        UPDATEINTERVAL, 0, 0, updateTimerCallBack, NULL
    );
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), timer, kCFRunLoopDefaultMode);
    CFRunLoopRun();

    exit(0);
}

// Проверка элемента из описания жеста
int checkMask(char mask, const int64_t diff, const int64_t prevdiff) {
    switch (mask) {
        case '+': return diff > 0;
        case '-': return diff < 0;
        case '0': return diff == 0;
        case 'v': return prevdiff > diff;
        case '^': return diff > prevdiff;
    }

    return 0;
}

// Проверка жеста
int checkGesture(uint64_t current) {
    static uint64_t window[WINDOWSIZE];
    static uint64_t prevdiff = 0;
    static gesturepos = 0;

    uint64_t average = current;
    int i, notzerocnt = 1;

    for (i = WINDOWSIZE-1; i > 0; i--) {
        if (window[i] = window[i-1]) {
            average += window[i];
            notzerocnt++;
        }
    }

    window[0] = current;
    average /= notzerocnt;

    int64_t diff = average - current;
    if (checkMask(GESTURES[gesturepos], diff, prevdiff)) {
        gesturepos++;
    } else {
        if (!gesturepos || !checkMask(GESTURES[gesturepos-1], diff, prevdiff)) {
            gesturepos = 0;
        }
    }

    prevdiff = diff;
    return gesturepos == sizeof GESTURES - 1;
}