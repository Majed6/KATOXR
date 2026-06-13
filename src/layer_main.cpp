#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>
#include <mutex>
#include <cstring>
#include <set>
#include "KATNativeSDK.h"
#include "Treadmill.h"

// Global state
std::recursive_mutex g_mutex;
std::set<XrAction> g_locomotionActions;
std::set<XrAction> g_locomotionActionsX;
std::set<XrAction> g_locomotionActionsY;
XrSpace g_localSpace = XR_NULL_HANDLE;
XrSpace g_viewSpace = XR_NULL_HANDLE;
XrSession g_currentSession = XR_NULL_HANDLE;
XrPath g_leftHandPath = XR_NULL_PATH;
bool g_isOpenComposite = false;

// Global state for KAT integration
Treadmill g_treadmill;
ObjectTransform g_eye;             // Mock for HMD Transform

void ProcessKatMovement(const TreadMillData& ws) {
    g_treadmill.Update(ws, g_eye);
}

// Function pointers for the next layer
PFN_xrGetInstanceProcAddr next_xrGetInstanceProcAddr = nullptr;
PFN_xrCreateAction next_xrCreateAction = nullptr;
PFN_xrDestroyAction next_xrDestroyAction = nullptr;
PFN_xrSyncActions next_xrSyncActions = nullptr;
PFN_xrGetActionStateFloat next_xrGetActionStateFloat = nullptr;
PFN_xrGetActionStateVector2f next_xrGetActionStateVector2f = nullptr;
PFN_xrSuggestInteractionProfileBindings next_xrSuggestInteractionProfileBindings = nullptr;
PFN_xrPathToString next_xrPathToString = nullptr;
PFN_xrStringToPath next_xrStringToPath = nullptr;
PFN_xrDestroyInstance next_xrDestroyInstance = nullptr;
PFN_xrWaitFrame next_xrWaitFrame = nullptr;
PFN_xrLocateSpace next_xrLocateSpace = nullptr;
PFN_xrLocateViews next_xrLocateViews = nullptr;
PFN_xrCreateReferenceSpace next_xrCreateReferenceSpace = nullptr;

void UpdateEyeRotation(const XrQuaternionf& orientation) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_eye.rotation.x = orientation.x;
    g_eye.rotation.y = orientation.y;
    g_eye.rotation.z = orientation.z;
    g_eye.rotation.w = orientation.w;
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrWaitFrame(
    XrSession session,
    const XrFrameWaitInfo* frameWaitInfo,
    XrFrameState* frameState) {
    if (!next_xrWaitFrame) return XR_ERROR_FUNCTION_UNSUPPORTED;
    XrResult result = next_xrWaitFrame(session, frameWaitInfo, frameState);
    if (XR_SUCCEEDED(result)) {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (session != g_currentSession) {
            g_localSpace = XR_NULL_HANDLE;
            g_viewSpace = XR_NULL_HANDLE;
            g_currentSession = session;
        }

        if (g_localSpace == XR_NULL_HANDLE && next_xrCreateReferenceSpace) {
            XrReferenceSpaceCreateInfo createInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
            createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            createInfo.poseInReferenceSpace.orientation.w = 1.0f;
            next_xrCreateReferenceSpace(session, &createInfo, &g_localSpace);
        }
        if (g_viewSpace == XR_NULL_HANDLE && next_xrCreateReferenceSpace) {
            XrReferenceSpaceCreateInfo createInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
            createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
            createInfo.poseInReferenceSpace.orientation.w = 1.0f;
            next_xrCreateReferenceSpace(session, &createInfo, &g_viewSpace);
        }

        if (g_localSpace != XR_NULL_HANDLE && g_viewSpace != XR_NULL_HANDLE && next_xrLocateSpace) {
            XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
            XrResult locResult = next_xrLocateSpace(g_viewSpace, g_localSpace, frameState->predictedDisplayTime, &location);
            if (XR_SUCCEEDED(locResult) && (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                UpdateEyeRotation(location.pose.orientation);
            }
        }
    }
    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrLocateViews(
    XrSession session,
    const XrViewLocateInfo* viewLocateInfo,
    XrViewState* viewState,
    uint32_t viewCapacityInput,
    uint32_t* viewCountOutput,
    XrView* views) {
    if (!next_xrLocateViews) return XR_ERROR_FUNCTION_UNSUPPORTED;

    XrResult result = next_xrLocateViews(
        session, viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);
    if (g_isOpenComposite && XR_SUCCEEDED(result) && viewState && views && viewCountOutput &&
        *viewCountOutput > 0 && viewCapacityInput > 0 &&
        (viewState->viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        UpdateEyeRotation(views[0].pose.orientation);
    }
    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrSyncActions(
    XrSession session,
    const XrActionsSyncInfo* syncInfo) {
    
    if (!next_xrSyncActions) return XR_ERROR_FUNCTION_UNSUPPORTED;

    TreadMillData ws = KATNativeSDK::GetWalkStatus();
    
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (ws.connected != g_treadmill.IsConnected()) {
            g_treadmill.SetConnected(ws.connected);
        }
        
        ProcessKatMovement(ws);
    }

    return next_xrSyncActions(session, syncInfo);
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrCreateAction(
    XrActionSet actionSet,
    const XrActionCreateInfo* createInfo,
    XrAction* action) {
    if (!next_xrCreateAction) return XR_ERROR_FUNCTION_UNSUPPORTED;
    XrResult result = next_xrCreateAction(actionSet, createInfo, action);
    if (XR_SUCCEEDED(result)) {
        {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            // OpenComposite exposes legacy OpenVR Axis0 as separate float actions.
            if (strcmp(createInfo->actionName, "legacy-left-thumbstick-x") == 0 ||
                strcmp(createInfo->actionName, "legacy-left-trackpad-x") == 0) {
                g_locomotionActionsX.insert(*action);
            } else if (strcmp(createInfo->actionName, "legacy-left-thumbstick-y") == 0 ||
                       strcmp(createInfo->actionName, "legacy-left-trackpad-y") == 0) {
                g_locomotionActionsY.insert(*action);
            }
        }
    }
    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrSuggestInteractionProfileBindings(
    XrInstance instance,
    const XrInteractionProfileSuggestedBinding* suggestedBindings) {
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < suggestedBindings->countSuggestedBindings; ++i) {
        char pathStr[XR_MAX_PATH_LENGTH];
        if (next_xrPathToString && next_xrPathToString(instance, suggestedBindings->suggestedBindings[i].binding, XR_MAX_PATH_LENGTH, &count, pathStr) == XR_SUCCESS) {
            XrAction action = suggestedBindings->suggestedBindings[i].action;

            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            if (strcmp(pathStr, "/user/hand/left/input/thumbstick") == 0 ||
                strcmp(pathStr, "/user/hand/left/input/trackpad") == 0) {
                g_locomotionActions.insert(action);
            } else if (strcmp(pathStr, "/user/hand/left/input/thumbstick/x") == 0 ||
                       strcmp(pathStr, "/user/hand/left/input/trackpad/x") == 0) {
                g_locomotionActionsX.insert(action);
            } else if (strcmp(pathStr, "/user/hand/left/input/thumbstick/y") == 0 ||
                       strcmp(pathStr, "/user/hand/left/input/trackpad/y") == 0) {
                g_locomotionActionsY.insert(action);
            }
        }
    }

    if (next_xrSuggestInteractionProfileBindings) {
        return next_xrSuggestInteractionProfileBindings(instance, suggestedBindings);
    }
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrDestroyAction(XrAction action) {
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        g_locomotionActions.erase(action);
        g_locomotionActionsX.erase(action);
        g_locomotionActionsY.erase(action);
    }
    if (next_xrDestroyAction) return next_xrDestroyAction(action);
    return XR_SUCCESS;
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrDestroyInstance(XrInstance instance) {
    XrResult result = XR_SUCCESS;
    if (next_xrDestroyInstance) {
        result = next_xrDestroyInstance(instance);
    }
    
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    g_localSpace = XR_NULL_HANDLE;
    g_viewSpace = XR_NULL_HANDLE;
    g_currentSession = XR_NULL_HANDLE;
    g_leftHandPath = XR_NULL_PATH;
    g_isOpenComposite = false;
    g_locomotionActions.clear();
    g_locomotionActionsX.clear();
    g_locomotionActionsY.clear();
    g_eye = {};
    g_treadmill = Treadmill();
    return result;
}

bool AlterStickInput(XrVector2f& input) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    return g_treadmill.AlterStickInput(input);
}

bool IsLeftHandOrUnspecifiedSubaction(XrPath subactionPath) {
    std::lock_guard<std::recursive_mutex> lock(g_mutex);
    return subactionPath == XR_NULL_PATH ||
           (g_leftHandPath != XR_NULL_PATH && subactionPath == g_leftHandPath);
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrGetActionStateFloat(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateFloat* state) {
    if (!next_xrGetActionStateFloat) return XR_ERROR_FUNCTION_UNSUPPORTED;
    XrResult result = next_xrGetActionStateFloat(session, getInfo, state);
    if (XR_SUCCEEDED(result) && state->isActive) {
        int locoComponent = 0; // 1 for X, 2 for Y
        {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            if (g_locomotionActionsX.count(getInfo->action)) locoComponent = 1;
            else if (g_locomotionActionsY.count(getInfo->action)) locoComponent = 2;
        }

        if (locoComponent != 0 && IsLeftHandOrUnspecifiedSubaction(getInfo->subactionPath)) {
            XrVector2f val = {0, 0};
            if (locoComponent == 1) val.x = state->currentState;
            else val.y = state->currentState;

            if (AlterStickInput(val)) {
                state->currentState = (locoComponent == 1) ? val.x : val.y;
                state->changedSinceLastSync = XR_TRUE;
            }
        }
    }
    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrGetActionStateVector2f(
    XrSession session,
    const XrActionStateGetInfo* getInfo,
    XrActionStateVector2f* state) {
    if (!next_xrGetActionStateVector2f) return XR_ERROR_FUNCTION_UNSUPPORTED;
    XrResult result = next_xrGetActionStateVector2f(session, getInfo, state);
    if (XR_SUCCEEDED(result) && state->isActive) {
        bool isLocomotion = false;
        {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            if (g_locomotionActions.count(getInfo->action)) {
                isLocomotion = true;
            }
        }

        if (isLocomotion && IsLeftHandOrUnspecifiedSubaction(getInfo->subactionPath)) {
            if (AlterStickInput(state->currentState)) {
                state->changedSinceLastSync = XR_TRUE;
            }
        }
    }
    return result;
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrGetInstanceProcAddr(
    XrInstance instance,
    const char* name,
    PFN_xrVoidFunction* function) {
    
    if (strcmp(name, "xrGetActionStateVector2f") == 0) {
        *function = (PFN_xrVoidFunction)KATOXR_xrGetActionStateVector2f;
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrSyncActions") == 0) {
        *function = (PFN_xrVoidFunction)KATOXR_xrSyncActions;
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrGetActionStateFloat") == 0) {
        *function = (PFN_xrVoidFunction)KATOXR_xrGetActionStateFloat;
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrSuggestInteractionProfileBindings") == 0) {
        *function = (PFN_xrVoidFunction)KATOXR_xrSuggestInteractionProfileBindings;
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrCreateAction") == 0) {
        *function = (PFN_xrVoidFunction)KATOXR_xrCreateAction;
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrDestroyAction") == 0) {
        *function = (PFN_xrVoidFunction)KATOXR_xrDestroyAction;
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrDestroyInstance") == 0) {
        *function = (PFN_xrVoidFunction)KATOXR_xrDestroyInstance;
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrWaitFrame") == 0) {
        *function = (PFN_xrVoidFunction)KATOXR_xrWaitFrame;
        return XR_SUCCESS;
    }
    if (strcmp(name, "xrLocateViews") == 0) {
        *function = (PFN_xrVoidFunction)KATOXR_xrLocateViews;
        return XR_SUCCESS;
    }
    
    // Fallback to the next layer
    return next_xrGetInstanceProcAddr(instance, name, function);
}

XRAPI_ATTR XrResult XRAPI_CALL KATOXR_xrCreateApiLayerInstance(
    const XrInstanceCreateInfo* info,
    const XrApiLayerCreateInfo* apiLayerInfo,
    XrInstance* out_instance) {
    
    next_xrGetInstanceProcAddr = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;
    
    XrApiLayerCreateInfo nextApiLayerInfo = *apiLayerInfo;
    nextApiLayerInfo.nextInfo = apiLayerInfo->nextInfo->next;
    
    XrResult result = apiLayerInfo->nextInfo->nextCreateApiLayerInstance(info, &nextApiLayerInfo, out_instance);
    if (XR_SUCCEEDED(result)) {
        next_xrGetInstanceProcAddr(*out_instance, "xrGetActionStateVector2f", (PFN_xrVoidFunction*)&next_xrGetActionStateVector2f);
        next_xrGetInstanceProcAddr(*out_instance, "xrSyncActions", (PFN_xrVoidFunction*)&next_xrSyncActions);
        next_xrGetInstanceProcAddr(*out_instance, "xrGetActionStateFloat", (PFN_xrVoidFunction*)&next_xrGetActionStateFloat);
        next_xrGetInstanceProcAddr(*out_instance, "xrSuggestInteractionProfileBindings", (PFN_xrVoidFunction*)&next_xrSuggestInteractionProfileBindings);
        next_xrGetInstanceProcAddr(*out_instance, "xrPathToString", (PFN_xrVoidFunction*)&next_xrPathToString);
        next_xrGetInstanceProcAddr(*out_instance, "xrStringToPath", (PFN_xrVoidFunction*)&next_xrStringToPath);
        next_xrGetInstanceProcAddr(*out_instance, "xrCreateAction", (PFN_xrVoidFunction*)&next_xrCreateAction);
        next_xrGetInstanceProcAddr(*out_instance, "xrDestroyAction", (PFN_xrVoidFunction*)&next_xrDestroyAction);
        next_xrGetInstanceProcAddr(*out_instance, "xrDestroyInstance", (PFN_xrVoidFunction*)&next_xrDestroyInstance);
        next_xrGetInstanceProcAddr(*out_instance, "xrWaitFrame", (PFN_xrVoidFunction*)&next_xrWaitFrame);
        next_xrGetInstanceProcAddr(*out_instance, "xrLocateSpace", (PFN_xrVoidFunction*)&next_xrLocateSpace);
        next_xrGetInstanceProcAddr(*out_instance, "xrLocateViews", (PFN_xrVoidFunction*)&next_xrLocateViews);
        next_xrGetInstanceProcAddr(*out_instance, "xrCreateReferenceSpace", (PFN_xrVoidFunction*)&next_xrCreateReferenceSpace);

        {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            g_isOpenComposite =
                strstr(info->applicationInfo.applicationName, "OpenComposite") != nullptr;
            if (next_xrStringToPath) {
                XrResult pathResult = next_xrStringToPath(*out_instance, "/user/hand/left", &g_leftHandPath);
                if (XR_FAILED(pathResult)) {
                    g_leftHandPath = XR_NULL_PATH;
                }
            }
        }
    }
    return result;
}

#ifdef _WIN32
#define LAYER_EXPORT __declspec(dllexport)
#else
#define LAYER_EXPORT
#endif

extern "C" {
LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo,
    const char* layerName,
    XrNegotiateApiLayerRequest* layerRequest) {
    
    if (loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
        loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    if (layerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
        layerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
        layerRequest->structSize != sizeof(XrNegotiateApiLayerRequest)) {
        return XR_ERROR_INITIALIZATION_FAILED;
    }

    layerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    layerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
    layerRequest->getInstanceProcAddr = KATOXR_xrGetInstanceProcAddr;
    layerRequest->createApiLayerInstance = KATOXR_xrCreateApiLayerInstance;

    return XR_SUCCESS;
}
}
