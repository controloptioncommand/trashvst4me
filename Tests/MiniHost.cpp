// Minimal VST3 host used to smoke-test the built Burial Voice binary (Linux natively,
// Windows build under Wine): loads the module, instantiates the effect, lists parameters,
// switches a preset and pushes audio through it.
#define INIT_CLASS_IID
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/gui/iplugview.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef __APPLE__
 #include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef _WIN32
 #include <windows.h>
 using Handle = HMODULE;
 static Handle openLib (const char* p) { return LoadLibraryA (p); }
 static void* sym (Handle h, const char* n) { return (void*) GetProcAddress (h, n); }
#else
 #include <dlfcn.h>
 using Handle = void*;
 static Handle openLib (const char* p) { return dlopen (p, RTLD_NOW | RTLD_LOCAL); }
 static void* sym (Handle h, const char* n) { return dlsym (h, n); }
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

static std::string toUtf8 (const String128 s)
{
    std::string r;
    for (int i = 0; i < 128 && s[i]; ++i) r += (char) (s[i] < 128 ? s[i] : '?');
    return r;
}

static int fail (const char* msg) { std::printf ("FAIL: %s\n", msg); return 1; }

int main (int argc, char** argv)
{
    if (argc < 2) return fail ("usage: MiniHost <path to plugin binary>");

    Handle lib = openLib (argv[1]);
    if (! lib)
    {
#ifndef _WIN32
        std::printf ("%s\n", dlerror());
#endif
        return fail ("could not load module");
    }

#ifdef _WIN32
    if (auto init = (bool (*)()) sym (lib, "InitDll")) init();
#elif defined (__APPLE__)
    // argv[1] is <bundle>/Contents/MacOS/<binary>; bundleEntry wants the .vst3 bundle itself
    CFBundleRef bundle = nullptr;
    {
        std::string path (argv[1]);
        for (int up = 0; up < 3; ++up) path = path.substr (0, path.find_last_of ('/'));
        auto url = CFURLCreateFromFileSystemRepresentation (nullptr, (const UInt8*) path.c_str(), (CFIndex) path.size(), true);
        bundle = CFBundleCreate (nullptr, url);
        CFRelease (url);
    }
    if (auto entry = (bool (*)(CFBundleRef)) sym (lib, "bundleEntry"))
        if (! entry (bundle)) return fail ("bundleEntry");
#else
    if (auto entry = (bool (*)(void*)) sym (lib, "ModuleEntry")) entry (lib);
#endif

    auto getFactory = (IPluginFactory* (*)()) sym (lib, "GetPluginFactory");
    if (! getFactory) return fail ("no GetPluginFactory export");
    IPluginFactory* factory = getFactory();
    if (! factory) return fail ("factory is null");

    PFactoryInfo fi;
    factory->getFactoryInfo (&fi);
    std::printf ("vendor: %s\n", fi.vendor);

    IComponent* comp = nullptr;
    for (int32 i = 0; i < factory->countClasses(); ++i)
    {
        PClassInfo ci;
        factory->getClassInfo (i, &ci);
        std::printf ("class %d: %s [%s]\n", i, ci.name, ci.category);
        if (std::strcmp (ci.category, kVstAudioEffectClass) == 0)
            factory->createInstance (ci.cid, IComponent::iid, (void**) &comp);
    }
    if (! comp) return fail ("could not create audio effect");
    if (comp->initialize (nullptr) != kResultOk) return fail ("initialize");

    IAudioProcessor* proc = nullptr;
    comp->queryInterface (IAudioProcessor::iid, (void**) &proc);
    if (! proc) return fail ("no IAudioProcessor");

    IEditController* ctrl = nullptr;
    comp->queryInterface (IEditController::iid, (void**) &ctrl);
    bool separateController = false;
    if (! ctrl)
    {
        TUID cid;
        if (comp->getControllerClassId (cid) == kResultOk)
            factory->createInstance (cid, IEditController::iid, (void**) &ctrl);
        if (! ctrl || ctrl->initialize (nullptr) != kResultOk) return fail ("no edit controller");
        separateController = true;
    }

    IConnectionPoint *compCP = nullptr, *ctrlCP = nullptr;
    comp->queryInterface (IConnectionPoint::iid, (void**) &compCP);
    ctrl->queryInterface (IConnectionPoint::iid, (void**) &ctrlCP);
    if (separateController && compCP && ctrlCP)
    {
        compCP->connect (ctrlCP);
        ctrlCP->connect (compCP);
    }

    const int32 numParams = ctrl->getParameterCount();
    std::printf ("parameters: %d\n", numParams);
    for (int32 i = 0; i < numParams; ++i)
    {
        ParameterInfo pi;
        ctrl->getParameterInfo (i, pi);
        String128 txt;
        ctrl->getParamStringByValue (pi.id, ctrl->getParamNormalized (pi.id), txt);
        std::printf ("  %-14s = %s\n", toUtf8 (pi.title).c_str(), toUtf8 (txt).c_str());
    }

    SpeakerArrangement stereo = SpeakerArr::kStereo;
    if (proc->setBusArrangements (&stereo, 1, &stereo, 1) != kResultOk) return fail ("stereo arrangement");
    comp->activateBus (kAudio, kInput, 0, true);
    comp->activateBus (kAudio, kOutput, 0, true);

    constexpr int block = 512;
    constexpr double fs = 48000.0;
    ProcessSetup setup { kRealtime, kSample32, block, fs };
    if (proc->setupProcessing (setup) != kResultOk) return fail ("setupProcessing");
    if (comp->setActive (true) != kResultOk) return fail ("setActive");
    proc->setProcessing (true);

    std::printf ("latency: %u samples, tail: %u samples\n", proc->getLatencySamples(), proc->getTailSamples());

    std::vector<float> inL (block), inR (block), outL (block), outR (block);
    float* ins[] = { inL.data(), inR.data() };
    float* outs[] = { outL.data(), outR.data() };
    AudioBusBuffers inBus, outBus;
    inBus.numChannels = 2;  inBus.channelBuffers32 = ins;
    outBus.numChannels = 2; outBus.channelBuffers32 = outs;

    ProcessData data;
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = block;
    data.numInputs = 1;  data.inputs = &inBus;
    data.numOutputs = 1; data.outputs = &outBus;

    double inE = 0, outE = 0;
    bool finite = true;
    long t = 0;
    const int blocks = (int) (4.0 * fs / block);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < block; ++i, ++t)
        {
            // buzzy 180 Hz tone with a few harmonics
            double v = 0;
            for (int h = 1; h <= 12; ++h) v += std::sin (2.0 * M_PI * 180.0 * h * t / fs) / h;
            inL[i] = inR[i] = (float) (0.2 * v);
        }
        if (proc->process (data) != kResultOk) return fail ("process");
        if (b > blocks / 2)
            for (int i = 0; i < block; ++i)
            {
                finite &= std::isfinite (outL[i]) && std::isfinite (outR[i]);
                inE += inL[i] * inL[i];
                outE += outL[i] * outL[i];
            }
    }

    const double ratioDb = 10.0 * std::log10 ((outE + 1e-20) / (inE + 1e-20));
    std::printf ("processed %d blocks: finite=%s, output/input level %+.1f dB\n", blocks, finite ? "yes" : "no", ratioDb);

    proc->setProcessing (false);
    comp->setActive (false);
    if (compCP && ctrlCP) { compCP->disconnect (ctrlCP); ctrlCP->disconnect (compCP); }
    if (compCP) compCP->release();
    if (ctrlCP) ctrlCP->release();
    if (separateController) ctrl->terminate();
    ctrl->release();
    proc->release();
    comp->terminate();
    comp->release();

#if defined (__APPLE__)
    if (auto exitFn = (bool (*)()) sym (lib, "bundleExit")) exitFn();
#elif ! defined (_WIN32)
    if (auto exitFn = (bool (*)()) sym (lib, "ModuleExit")) exitFn();
#endif

    if (! finite || outE < 1e-6) return fail ("output is broken or silent");
    std::printf ("PASS\n");
    return 0;
}
