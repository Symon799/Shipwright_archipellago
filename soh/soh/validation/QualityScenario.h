#pragma once
#ifdef __cplusplus
#include <imgui.h>
namespace SohValidation {
void ObserveMultiMarker(ImVec2 min, ImVec2 max);
void ObservePopup(ImVec2 min, ImVec2 max);
} // namespace SohValidation
extern "C" {
#endif
int SohValidation_Configure(int argc, char** argv);
void SohValidation_Register(void);
int SohValidation_Finish(void);
#ifdef __cplusplus
}
#endif
