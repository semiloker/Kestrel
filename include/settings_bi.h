#ifndef SETTINGS_BI_H
#define SETTINGS_BI_H

#include <map>
#include <string>
#include <vector>

#include "interfaces_bi.h"

class resource_usage_bi;
class overlay_bi;
class draw_batteryinfo_bi;
class batteryinfo_bi;

class settings_bi
{
public:
    Result<void> load();
    Result<void> save() const;

    bool getBool(const char *key, bool def) const;
    int getInt(const char *key, int def) const;
    float getFloat(const char *key, float def) const;
    std::string getString(const char *key, const std::string &def) const;

    void setBool(const char *key, bool value);
    void setInt(const char *key, int value);
    void setFloat(const char *key, float value);
    void setString(const char *key, const std::string &value);

    void applyTo(resource_usage_bi *ru, overlay_bi *ov, draw_batteryinfo_bi *draw,
                 batteryinfo_bi *bi) const;
    void collectFrom(const resource_usage_bi *ru, const overlay_bi *ov,
                     const draw_batteryinfo_bi *draw, const batteryinfo_bi *bi);

    bool exportJson(const char *path) const;
    bool importJson(const char *path);

    void setProfile(const std::string &exe);
    std::string currentProfile() const { return activeProfile; }
    bool hasProfile(const std::string &exe) const;
    bool saveProfile(const std::string &exe, resource_usage_bi *ru, overlay_bi *ov,
                     draw_batteryinfo_bi *draw, batteryinfo_bi *bi);
    bool deleteProfile(const std::string &exe);
    std::vector<std::string> profileList() const;

private:
    std::string getValue(const char *key) const;

    std::map<std::string, std::string> values;
    std::map<std::string, std::map<std::string, std::string>> profiles;
    std::string activeProfile;
};

#endif
