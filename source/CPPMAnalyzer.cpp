#include <cmath>

#include "CPPMAnalyzer.h"
#include "CPPMAnalyzerSettings.h"
#include <AnalyzerChannelData.h>

CPPMAnalyzer::CPPMAnalyzer()
    :   Analyzer2(),
        mSettings(new CPPMAnalyzerSettings()),
        mSimulationInitilized(false)
{
    SetAnalyzerSettings(mSettings.get());
}

CPPMAnalyzer::~CPPMAnalyzer()
{
    KillThread();
}

void CPPMAnalyzer::SetupResults()
{
    mResults.reset(new CPPMAnalyzerResults(this, mSettings.get()));
    SetAnalyzerResults(mResults.get());
    mResults->AddChannelBubblesWillAppearOn(mSettings->mInputChannel);
}

void CPPMAnalyzer::WorkerThread()
{
    mSampleRateHz = GetSampleRate();

    mCPPM = GetAnalyzerChannelData(mSettings->mInputChannel);

    // Wait for a clean start
    for (;;) {
        while (SamplesToUs(mCPPM->GetSampleOfNextEdge() - mCPPM->GetSampleNumber()) < mSettings->mSyncTime) {
            mCPPM->AdvanceToNextEdge();
        }

        mCPPM->AdvanceToNextEdge();
        if (CorrectSyncDir(mCPPM->GetBitState())) {
            break;
        }
    }

    //let's put a dot exactly where we sample this bit:
    mResults->AddMarker(mCPPM->GetSampleNumber(), AnalyzerResults::Dot, mSettings->mInputChannel);

    U8 channel = 0;
    std::vector<double> prevs(mSettings->mMaxChan);
    for (;;) {
        U64 start = mCPPM->GetSampleNumber();
        mCPPM->AdvanceToNextEdge();

        U64 high = mCPPM->GetSampleNumber();

        mCPPM->AdvanceToNextEdge();

        U64 end = mCPPM->GetSampleNumber();

        // A complete PPM frame is about 22.5 ms (can vary between
        // manufacturer).[5] Signal low state is always 0.3 ms. It
        // begins with a start frame (state high for more than 2
        // ms). Each channel (up to 8) is encoded by the time of the
        // high state (PPM high state + 0.3 x (PPM low state) = servo
        // PWM pulse width).
        //
        // The above, while copied from my friendly wikipedia page,
        // was shown to be incorrect when frsky was giving me .4ms low
        // states.  Let's just figure it out.
        U64 width = SamplesToUs(end - start);

        if (width >= mSettings->mSyncTime && CorrectSyncDir(mCPPM->GetBitState())) {
            channel = 0;

            continue;
        }

        channel++;

        Frame frame;
        frame.mData1 = width;
        frame.mData2 = channel;
        frame.mFlags = 0;
        frame.mType = 0;
        frame.mStartingSampleInclusive = high;
        frame.mEndingSampleInclusive = mCPPM->GetSampleNumber();


        if (channel > mSettings->mMaxChan) {
            mResults->AddMarker(end - ((end - high)/2),
                                AnalyzerResults::ErrorX, mSettings->mInputChannel);
            frame.mFlags |= DISPLAY_AS_ERROR_FLAG;
            channel = 0;
            frame.mData2 = 1;
        } else if (std::abs(double(width) - prevs[channel-1]) >= mSettings->mMinChange
                   && prevs[channel-1] != width) {
            mResults->AddMarker(end - ((end - high)/2),
                                width > prevs[channel-1] ? AnalyzerResults::UpArrow : AnalyzerResults::DownArrow,
                                mSettings->mInputChannel);
            prevs[channel-1] = width;
            frame.mType = 1;
        }

        mResults->AddFrame(frame);
        mResults->CommitResults();
        ReportProgress(frame.mEndingSampleInclusive);
    }
}

bool CPPMAnalyzer::NeedsRerun()
{
    return false;
}

U32 CPPMAnalyzer::GenerateSimulationData(U64 minimum_sample_index, U32 device_sample_rate, SimulationChannelDescriptor **simulation_channels)
{
    if (mSimulationInitilized == false) {
        mSimulationDataGenerator.Initialize(GetSimulationSampleRate(), mSettings.get());
        mSimulationInitilized = true;
    }

    return mSimulationDataGenerator.GenerateSimulationData(minimum_sample_index, device_sample_rate, simulation_channels);
}

U32 CPPMAnalyzer::GetMinimumSampleRateHz()
{
    return 1000000;
}

const char *CPPMAnalyzer::GetAnalyzerName() const
{
    return "CPPM";
}

const char *GetAnalyzerName()
{
    return "CPPM";
}

Analyzer *CreateAnalyzer()
{
    return new CPPMAnalyzer();
}

void DestroyAnalyzer(Analyzer *analyzer)
{
    delete analyzer;
}

U64 CPPMAnalyzer::SamplesToUs(U64 samples)
{
    return (samples * 1000000) / mSampleRateHz;
}

bool CPPMAnalyzer::CorrectSyncDir(BitState state)
{
    // These appear backwards from the settings as they specify the
    // level before the sync, not at it.
    switch (mSettings->mSyncDir) {
    case 1:
        return state == BIT_LOW;
    case 2:
        return state == BIT_HIGH;
    }
    // default -- auto
    return true;
}
