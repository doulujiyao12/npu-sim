#pragma once

int GetDefinedParam(string var);
void ParseSimulationType(json j);
void ParseWorkloadConfig(json j);
void ParseHardwareConfig(json j);

template<typename T>
void SetParamFromJson(json j, string field, T *target);
template<typename T>
void SetParamFromJson(json j, string field, T *target, T default_value);