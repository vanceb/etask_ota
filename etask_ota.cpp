#include <etask_ota.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Regexp.h>
#include <HttpsOTAUpdate.h>

/* A text representation of the chip ID derived from the MAC */
char id[CHIP_ID_LEN];

void etask_ota(void * parameters) {
    for(;;) {
        /* Can't OTA without WiFi! */
        while (WiFi.status() != WL_CONNECTED) {
            delay(1000);
        }
        ota_update_check();
        delay(OTA_CHECK_INTERVAL);
    }
}

void ota_update(char * url) {
    HTTPClient https;
    https.begin(url, AWS_ROOT_CA_1);
    int httpCode = https.GET();
    if (httpCode == 200) {
        int total_size = https.getSize();
        int current_size = 0;
        size_t got = 0;
        Serial.printf("Update is %d bytes...\n", total_size);
        uint8_t buff[128] = {0};
        if (Update.begin(total_size)) {
            WiFiClient * stream = https.getStreamPtr();
            while (https.connected()) {
                size_t avail = stream->available();
                if (avail) {
                    got = stream->readBytes(buff, ((avail > sizeof(buff)) ? sizeof(buff) : avail));
                    Update.write(buff, got);
                    current_size += got;
                    if (current_size == total_size) {
                        if (Update.end()) {
                            Serial.printf("Update complete, downloaded %u bytes\n", current_size);
                            delay(1000);
                            if (Update.isFinished() && Update.canRollBack()) {
                                Serial.println("Rebooting to apply new firmware!");
                                //Update.rollBack();  // Switch to the other partition
                                delay(5000);
                                ESP.restart();  // Reboot to activate new software
                            } else {
                                Serial.println("All downloaded, but something isn't right - Not applying update");
                            }
                        } else {
                            Serial.printf("Update failed, code: %u\n", Update.getError());
                        }
                    }
                }
                delay(1);
            }
        } else {
            Serial.println("Update too large!");
        }
    } else {
        Serial.printf("(%d) Error getting firmware\n", httpCode);
    }
    https.end();
}

/* Check for new software and update as required */
void ota_update_check() {
    char url[OTA_URL_LENGTH];
    snprintf(url, OTA_URL_LENGTH, OTA_BASE_URL, id);
    Serial.printf("Checking latest firmware version: %s\n", url);

    HTTPClient https;
    https.begin(url, AWS_ROOT_CA_1);
    int httpCode = https.GET();
    if (httpCode == 200) {
        /* Success */
        String payload = https.getString();
        payload.trim();
        char latest[VERSION_LENGTH];
        strncpy(latest, payload.c_str(), VERSION_LENGTH);
        Serial.printf("(%d) Latest firmware version (%s)\n", httpCode, latest);
        /* simple check for different version
         * may use vercmp() if we don't want to allow downgrades
         */
        if (strcmp(latest, AUTO_VERSION)) {
            Serial.printf("Differs from current firmware (%s)\n", AUTO_VERSION);
            snprintf(url, OTA_URL_LENGTH, OTA_FIRMWARE_URL, PROJECT_NAME, latest);
            Serial.printf("Downloading latest firmware from %s\n", url);
            /* Start firmware upgrade */
            ota_update(url);
        } else {
            Serial.println("Using latest firmware - no update needed");
        }
    } else {
        Serial.printf("(%d) Failed to GET: %s\n", httpCode, url);
    }
    https.end();
}

/* Parse a git describe version number
 * Has one of two formats:
 *     Just the tagged version (Release), e.g. v1.2.56 (tag)
 *     Dev version, e.g. v1.2.56-12-asbd563 (tag-distance-hash)
 */
void parse_version(char * verstr, int &major, int &minor, int &point, int &step, char* hash) {
    char * start = verstr;
    char * istart;
    char * iend;
    char number[16];

    /* Setup fail state */
    major = -1;
    minor = -1;
    point = -1;
    step  = -1;
    *hash  = {'\0'};

    /* Preliminary (basic) check that we have a version string */
    if (!(strlen(verstr) > 0 && verstr[0] == 'v')) {
        return;
    }
    ++start;

    /* Parse the first integer (Major) */
    istart = start;
    iend = strchr(istart, '.');
    if (iend == NULL || (iend - istart) > 16) {
        Serial.print("Badly formatted version string: ");
        Serial.println(verstr);
        return;
    }
    strncpy(number, istart, (iend - istart));
    major = atoi(number);
    /* Parse the second integer (Minor) */
    istart = ++iend;
    iend = strchr(istart, '.');
    if (iend == NULL || (iend - istart) > 16) {
        Serial.print("Badly formatted version string: ");
        Serial.println(verstr);
        return;
    }
    strncpy(number, istart, (iend - istart));
    minor = atoi(number);
    /* Parse the third integer (Point) */
    istart = ++iend;
    iend = strchr(istart, '-');
    if (iend == NULL) {
        /* Presume we have a release version */
        point = atoi(istart);
        return;
    } else {
        /* We found a hyphen so presume dev version */
        istart = ++iend;
        iend = strchr(istart, '-');
        if (iend == NULL || (iend - istart) > 16) {
            Serial.print("Badly formatted version string: ");
            Serial.println(verstr);
            return;
        }
        strncpy(number, istart, (iend - istart));
        step = atoi(number);
        istart = ++iend;
        /* Grab the hash */
        strcpy(hash, istart);
    }

    /* Sanity check */
    if (major == 0 && minor == 0 && point == 0) {
        /* something went wrong */
        Serial.print("Error parsing version data from: ");
        Serial.println(verstr);
        major = -1;
        minor = -1;
        point = -1; 
        step  = -1;
        *hash  = {'\0'};
        return;
    }
}

/* Semantically compares version numbers
 * Returning a positive number if the target is
 * a later version than the current, a negative
 * number if earlier than current, or 0 if identical
 */
int vercmp(char * current, char * target) {
    int c_major, c_minor, c_point, c_step;
    char c_hash[VERSION_LENGTH];
    int t_major, t_minor, t_point, t_step;
    char t_hash[VERSION_LENGTH];

    parse_version(current, c_major, c_minor, c_point, c_step, c_hash);
    parse_version(target,  t_major, t_minor, t_point, t_step, t_hash);

    if (t_major > c_major)
        return 1;
    if (t_major < c_major)
        return -1;
    if (t_minor > c_minor)
        return 1;
    if (t_minor < c_minor)
        return -1;
    if (t_point > c_point)
        return 1;
    if (t_point < c_point)
        return -1;
    if (t_step > c_step)
        return 1;
    if (t_step < c_step)
        return -1;
    if (strcmp(t_hash, c_hash) == 0) {
        return 0;
    } else {
        return 100;
    }

}