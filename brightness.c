#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <ApplicationServices/ApplicationServices.h>


/* As of macOS 10.12.4, brightness set by public IOKit API is
   overridden by CoreDisplay's brightness (to support Night Shift). In
   addition, using CoreDisplay to get brightness supports additional
   display types, e.g. the 2015 iMac's internal display.

   The below functions in CoreDisplay seem to work to adjust the
   "user" brightness (like dragging the slider in System Preferences
   or using function keys).  The symbols below are listed in the .tbd
   file in CoreDisplay.framework so it is at least more "public" than
   a symbol in a private framework, though there are no public headers
   distributed for this framework. */

extern double CoreDisplay_Display_GetUserBrightness(CGDirectDisplayID id)
__attribute__((weak_import));
extern void CoreDisplay_Display_SetUserBrightness(CGDirectDisplayID id,
                                                  double brightness)
__attribute__((weak_import));

/* Some issues with the above CoreDisplay functions include:

   - There's no way to tell if setting the brightness was successful

   - There's no way to tell if a brightness of 1 means that the
     brightness is actually 1, or if there's no adjustable brightness

   - Brightness changes aren't reflected in System Preferences
     immediately

   - They don't work on Apple Silicon Macs

   Fixing these means using the private DisplayServices.framework.  Be
   even more careful about these.
*/
extern bool DisplayServicesCanChangeBrightness(CGDirectDisplayID id)
__attribute__((weak_import));
extern void DisplayServicesBrightnessChanged(CGDirectDisplayID id,
                                             double brightness)
__attribute__((weak_import));

/* Below functions are necessary on Apple Silicon/macOS 11. */
extern int DisplayServicesGetBrightness(CGDirectDisplayID id,
                                        float *brightness)
__attribute__((weak_import));
extern int DisplayServicesSetBrightness(CGDirectDisplayID id,
                                        float brightness)
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

__attribute__((noreturn))
static void usage() {
    fprintf(stderr, "usage: %s [-m|-d display] [-v] <brightness>\n", APP_NAME);
    fprintf(stderr, "   or: %s -l [-v]\n", APP_NAME);
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

/* CGDisplayIOServicePort is deprecated as of 10.9; try to match ourselves */
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

static bool setBrightness(CGDirectDisplayID dspy, io_service_t service,
                          float brightness) {
    /* 1. Try DisplayServices set SPI - more likely to work on
       recent macOS */
    if ((DisplayServicesSetBrightness != NULL) &&
        !DisplayServicesSetBrightness(dspy, brightness)) {
        return true;
    }

    /* 2. Try CoreDisplay SPI wrapped by DisplayServices (if available)
       to work around caveats as described above */
    if (CoreDisplay_Display_SetUserBrightness != NULL) {
        if ((DisplayServicesCanChangeBrightness != NULL) &&
            !DisplayServicesCanChangeBrightness(dspy)) {
            fprintf(stderr,
                    "%s: unable to set brightness of display 0x%x\n",
                    APP_NAME, (unsigned int)dspy);
            return false;
        }

        CoreDisplay_Display_SetUserBrightness(dspy, brightness);

        if (DisplayServicesBrightnessChanged != NULL)
            DisplayServicesBrightnessChanged(dspy, brightness);
        return true;
    }

    /* 3. Try IODisplay API */
    IOReturn err = IODisplaySetFloatParameter(service, kNilOptions,
                                              kDisplayBrightness, brightness);
    if (err != kIOReturnSuccess) {
        fprintf(stderr,
                "%s: failed to set brightness of display 0x%x (error %d)\n",
                APP_NAME, (unsigned int)dspy, err);
        return false;
    }

    return true;
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
