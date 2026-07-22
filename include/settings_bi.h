#ifndef SETTINGS_BI_H
#define SETTINGS_BI_H

#include <map>
#include <string>

class resource_usage_bi;
class overlay_bi;
class draw_batteryinfo_bi;
class batteryinfo_bi;

class settings_bi
{
public:
    void load();
    bool save() const;

    bool getBool(const char *key, bool def) const;
    int getInt(const char *key, int def) const;
    float getFloat(const char *key, float def) const;

    void setBool(const char *key, bool value);
    void setInt(const char *key, int value);
    void setFloat(const char *key, float value);

    void applyTo(resource_usage_bi *ru, overlay_bi *ov, draw_batteryinfo_bi *draw,
                 batteryinfo_bi *bi) const;
    void collectFrom(const resource_usage_bi *ru, const overlay_bi *ov,
                     const draw_batteryinfo_bi *draw, const batteryinfo_bi *bi);

private:
    std::map<std::string, std::string> values;
};

#endif
