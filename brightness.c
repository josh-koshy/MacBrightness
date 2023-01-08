#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <ApplicationServices/ApplicationServices.h>

extern int DisplayServicesSetBrightness(CGDirectDisplayID id, float brightness)
__attribute__((weak_import));
const int kMaxDisplays = 16;
const CFStringRef kDisplayBrightness = CFSTR(kIODisplayBrightnessKey);
const char *APP_NAME;
static void errexit(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", APP_NAME);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

static bool CFNumberEqualsUInt32(CFNumberRef number, uint32_t uint32) {
    if (number == NULL)
        return (uint32 == 0);

    /* there's no CFNumber type guaranteed to be a uint32, so pick
       something bigger that's guaranteed not to truncate */
    int64_t int64;
    if (!CFNumberGetValue(number, kCFNumberSInt64Type, &int64))
        return false;

    return (int64 == uint32);
}

static io_service_t CGDisplayGetIOServicePort(CGDirectDisplayID dspy) {
    uint32_t vendor = CGDisplayVendorNumber(dspy);
    uint32_t model = CGDisplayModelNumber(dspy); // == product ID
    uint32_t serial = CGDisplaySerialNumber(dspy);

    CFMutableDictionaryRef matching = IOServiceMatching("IODisplayConnect");

    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMasterPortDefault, matching, &iter))
        return 0;

    io_service_t service, matching_service = 0;
    while ( (service = IOIteratorNext(iter)) != 0) {
        CFDictionaryRef info = IODisplayCreateInfoDictionary(service, kIODisplayNoProductName);

        CFNumberRef vendorID = CFDictionaryGetValue(info, CFSTR(kDisplayVendorID));
        CFNumberRef productID = CFDictionaryGetValue(info, CFSTR(kDisplayProductID));
        CFNumberRef serialNumber = CFDictionaryGetValue(info, CFSTR(kDisplaySerialNumber));

        if (CFNumberEqualsUInt32(vendorID, vendor) &&
            CFNumberEqualsUInt32(productID, model) &&
            CFNumberEqualsUInt32(serialNumber, serial)) {
            matching_service = service;

            CFRelease(info);
            break;
        }

        CFRelease(info);
    }

    IOObjectRelease(iter);
    return matching_service;
}

static void setBrightness(CGDirectDisplayID dspy, io_service_t service,
                          float brightness) {
    /* 1. Try DisplayServices set SPI - more likely to work on
       recent macOS */
    if ((DisplayServicesSetBrightness != NULL) &&
        !DisplayServicesSetBrightness(dspy, brightness)) {
    }
}

float brightness = 1;

int main() {
    CGDirectDisplayID display[kMaxDisplays];
    CGDisplayCount numDisplays;
    CGDisplayErr err;
    err = CGGetOnlineDisplayList(kMaxDisplays, display, &numDisplays);
    if (err != CGDisplayNoErr)
        errexit("cannot get list of displays (error %d)\n", err);

    for (CGDisplayCount i = 0; i < numDisplays; ++i) {
        CGDirectDisplayID dspy = display[i];
        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(dspy);
        if (mode == NULL)
            continue;

        CGDisplayModeRelease(mode);

        io_service_t service = CGDisplayGetIOServicePort(dspy);
        setBrightness(dspy, service, brightness);
    }
    return 0;
}
