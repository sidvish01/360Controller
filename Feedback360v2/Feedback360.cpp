/*
 MICE Xbox 360 Controller driver for Mac OS X
 Copyright (C) 2006-2013 Colin Munro

 main.c - Main code for the FF plugin

 This file is part of Xbox360Controller.

 Xbox360Controller is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 Xbox360Controller is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 a int with Foobar; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
//#include <CoreFoundation/CoreFoundation.h>
#include "Feedback360.h"

#define LoopGranularity             10000         // Microseconds


// static var initialization
UInt32 Feedback360::sFactoryRefCount = 0;

IOCFPlugInInterface functionMap360_IOCFPlugInInterface={
    // Padding required for COM
    NULL,
    // IUnknown
    &Feedback360::sQueryInterface,
    &Feedback360::sAddRef,
    &Feedback360::sRelease,
    // IOCFPlugInInterface
    1,0,    // Version
    &Feedback360::sProbe,
    &Feedback360::sStart,
    &Feedback360::sStop
};

IOForceFeedbackDeviceInterface functionMap360_IOForceFeedbackDeviceInterface={
    // Padding required for COM
    NULL,
    // IUnknown
    &Feedback360::sQueryInterface,
    &Feedback360::sAddRef,
    &Feedback360::sRelease,
    // IOForceFeedbackDevice
    &Feedback360::sGetVersion,
    &Feedback360::sInitializeTerminate,
    &Feedback360::sDestroyEffect,
    &Feedback360::sDownloadEffect,
    &Feedback360::sEscape,
    &Feedback360::sGetEffectStatus,
    &Feedback360::sGetForceFeedbackCapabilities,
    &Feedback360::sGetForceFeedbackState,
    &Feedback360::sSendForceFeedbackCommand,
    &Feedback360::sSetProperty,
    &Feedback360::sStartEffect,
    &Feedback360::sStopEffect
};


Feedback360::Feedback360(void) : fRefCount(1)
{

    EffectCount = 0;
    EffectIndex = 1;
    EffectList = NULL;
    Stopped = TRUE;
    Paused = FALSE;
    PausedTime = 0;
    Gain = 10000;
    Actuator = TRUE;
    Manual = FALSE;
    PrvLeftLevel = 0;
    PrvRightLevel = 0;
    iIOCFPlugInInterface.pseudoVTable = (IUnknownVTbl *) &functionMap360_IOCFPlugInInterface;
    iIOCFPlugInInterface.obj = this;

    iIOForceFeedbackDeviceInterface.pseudoVTable = (IUnknownVTbl *) &functionMap360_IOForceFeedbackDeviceInterface;
    iIOForceFeedbackDeviceInterface.obj = this;

    sFactoryAddRef();



}

Feedback360::~Feedback360(void)
{
    sFactoryRelease();
}

HRESULT Feedback360::QueryInterface(REFIID iid, LPVOID *ppv)
{
    CFUUIDRef interface;
    interface = CFUUIDCreateFromUUIDBytes(NULL,iid);
    if(CFEqual(interface,kIOForceFeedbackDeviceInterfaceID))
        *ppv=&this->iIOForceFeedbackDeviceInterface;
    // IUnknown || IOCFPlugInInterface
    else if(CFEqual(interface,IUnknownUUID)||CFEqual(interface,kIOCFPlugInInterfaceID))
        *ppv=&this->iIOCFPlugInInterface;
    else
        *ppv=NULL;
    // Done
    CFRelease(interface);
    if((*ppv)==NULL) return E_NOINTERFACE;
    else {
        this->iIOCFPlugInInterface.pseudoVTable->AddRef(*ppv);
    }
    return FF_OK;
}

ULONG Feedback360::AddRef(void)
{
    fRefCount++;
    return fRefCount;
}

ULONG Feedback360::Release(void)
{
    ULONG returnValue = fRefCount - 1;
    if(returnValue > 0) {
        fRefCount = returnValue;
    } else if(returnValue == 0) {
        fRefCount = returnValue;
        delete this;
    } else {
        returnValue = 0;
    }
    return returnValue;
}

IOCFPlugInInterface** Feedback360::Alloc()
{
    Feedback360 *me = new Feedback360;
    fprintf (stderr, "Feedback360::alloc called\n");
    if(!me) {
        return NULL;
    }
    // return reinterpret_cast<IOCFPlugInInterface **>(&me->iIOCFPlugInInterface.pseudoVTable);
    return (IOCFPlugInInterface **)(&me->iIOCFPlugInInterface.pseudoVTable);
}

void Feedback360::sFactoryAddRef ( void )
{
    if ( sFactoryRefCount++ == 0 )
    {
        CFUUIDRef factoryID = kFeedback360Uuid;
        CFRetain ( factoryID );
        CFPlugInAddInstanceForFactory ( factoryID );
    }
}

void Feedback360::sFactoryRelease ( void )
{
    if ( sFactoryRefCount-- == 1 )
    {
        CFUUIDRef factoryID = kFeedback360Uuid;
        CFPlugInRemoveInstanceForFactory ( factoryID );
        CFRelease ( factoryID );
    }
}

IOReturn Feedback360::Probe(CFDictionaryRef propertyTable, io_service_t service, SInt32 *order)
{
    if ((service==0)
        || ((!IOObjectConformsTo(service,"Xbox360ControllerClass"))
            && (!IOObjectConformsTo(service,"Wireless360Controller")))) return kIOReturnBadArgument;
    return FF_OK;
}

IOReturn Feedback360::Start(CFDictionaryRef propertyTable,io_service_t service)
{
    return FF_OK;
}

IOReturn Feedback360::Stop()
{
    return FF_OK;
}


HRESULT Feedback360::SetProperty(FFProperty property, void *value)
{
    if(property != FFPROP_FFGAIN) {
        return FFERR_UNSUPPORTED;
    }

    // 変数宣言

    UInt32 NewGain = *((UInt32*)value);
    __block HRESULT Result = FF_OK;

    // ゲインを設定する
    dispatch_sync(Queue, ^{
        if( 1 <= NewGain && NewGain <= 10000 )
        {
            Gain = NewGain;
        } else {
            Gain = MAX( 1, MIN( NewGain, 10000 ) );
            Result = FF_TRUNCATED;
        }
    });

    return( Result );
}

HRESULT Feedback360::StartEffect(FFEffectDownloadID EffectHandle, FFEffectStartFlag Mode, UInt32 Count)
{
    dispatch_sync(Queue, ^{
    // ハンドルからエフェクトを検索する
    for( UInt32 Index = 0; Index < EffectCount; Index ++ )
    {
        if( EffectList[Index]->Handle == EffectHandle )
        {
            fprintf(stderr, "Playing effect: %s\n", CFStringGetCStringPtr(CFUUIDCreateString(NULL, EffectList[Index]->Type), kCFStringEncodingMacRoman));
            // エフェクトの再生を開始する
            EffectList[Index]->Status  = FFEGES_PLAYING;
            EffectList[Index]->PlayCount = Count;
            EffectList[Index]->StartTime = CFAbsoluteTimeGetCurrent();
            // 停止を解除する
            Stopped = FALSE;
        } else {
            // DIES_SOLO が指定されている場合は他のエフェクトを停止する
            if( Mode & FFES_SOLO )
            {
                EffectList[Index]->Status = NULL;
            }
        }
    }
    });
    return FF_OK;
}

HRESULT Feedback360::StopEffect(UInt32 EffectHandle)
{
    dispatch_sync(Queue, ^{
    // ハンドルからエフェクトを検索する
    for( LONG Index = 0; Index < EffectCount; Index ++ )
    {
        if( EffectList[Index]->Handle == EffectHandle )
        {
            // エフェクトを停止する
            EffectList[Index]->Status = NULL;
            break;
        }
    }
    });
    return( FF_OK );
}

HRESULT Feedback360::DownloadEffect(CFUUIDRef EffectType, FFEffectDownloadID *EffectHandle, FFEFFECT *DiEffect, FFEffectParameterFlag Flags)
{
    // 変数宣言
    __block HRESULT Result = FF_OK;
    __block Feedback360Effect *Effect = NULL;

    fprintf(stderr, "Downloading effect: %s\n", CFStringGetCStringPtr(CFUUIDCreateString(NULL, EffectType), kCFStringEncodingMacRoman));


    // DIEP_NODOWNLOAD の場合は無視する
    if( Flags & FFEP_NODOWNLOAD )
    {
        return( FF_OK );
    }

    dispatch_sync(Queue, ^{

        if( *EffectHandle == 0 )
        {
            // 新規エフェクトを作成する
            Effect = new Feedback360Effect();
            Effect->Handle = ( EffectIndex ++ );
            // エフェクト数を増やす
            EffectCount ++;
            // エフェクト リストを拡張する
            Feedback360Effect **NewEffectList;
            NewEffectList = (Feedback360Effect **)realloc( EffectList, sizeof(Feedback360Effect*) * EffectCount );
            if( NewEffectList == NULL )
            {
                Result = -1;

            } else {
                EffectList = NewEffectList;
                EffectList[EffectCount - 1] = Effect;
                // エフェクトのハンドルを返す
                *EffectHandle = Effect->Handle;
            }
        } else {
            // ハンドルからエフェクトを検索する
            for( LONG Index = 0; Index < EffectCount; Index ++ )
            {
                if( EffectList[Index]->Handle == *EffectHandle )
                {
                    Effect = EffectList[Index];
                    break;
                }
            }
        }

        if(Effect == NULL || Result == -1) {
            Result = FFERR_INTERNAL;
        }
        else {
            // 種類を設定する
            Effect->Type = EffectType;

            // Flags を設定する
            Effect->DiEffect.dwFlags = DiEffect->dwFlags;

            // DIEP_DURATION を設定する
            if( Flags & FFEP_DURATION )
            {
                Effect->DiEffect.dwDuration = DiEffect->dwDuration;
            }

            // DIEP_SAMPLEPERIOD を設定する
            if( Flags & FFEP_SAMPLEPERIOD )
            {
                Effect->DiEffect.dwSamplePeriod = DiEffect->dwSamplePeriod;
            }

            // DIEP_GAIN を設定する
            if( Flags & FFEP_GAIN )
            {
                Effect->DiEffect.dwGain = DiEffect->dwGain;
                fprintf(stderr, "GAIN: %d; ", DiEffect->dwGain);
            }

            // DIEP_TRIGGERBUTTON を設定する
            if( Flags & FFEP_TRIGGERBUTTON )
            {
                Effect->DiEffect.dwTriggerButton = DiEffect->dwTriggerButton;
            }

            // DIEP_TRIGGERREPEATINTERVAL を設定する
            if( Flags & FFEP_TRIGGERREPEATINTERVAL )
            {
                Effect->DiEffect.dwTriggerRepeatInterval = DiEffect->dwTriggerRepeatInterval;
            }

            // DIEP_AXES を設定する
            if( Flags & FFEP_AXES )
            {
                Effect->DiEffect.cAxes  = DiEffect->cAxes;
                Effect->DiEffect.rgdwAxes = NULL;
            }

            // DIEP_DIRECTION を設定する
            if( Flags & FFEP_DIRECTION )
            {
                Effect->DiEffect.cAxes   = DiEffect->cAxes;
                Effect->DiEffect.rglDirection = NULL;
            }

            // DIEP_ENVELOPE を設定する
            if( ( Flags & FFEP_ENVELOPE ) && DiEffect->lpEnvelope != NULL )
            {
                memcpy( &Effect->DiEnvelope, DiEffect->lpEnvelope, sizeof( FFENVELOPE ) );
                // アタック時間とフェード時間がかぶる場合は値を補正する
                if( Effect->DiEffect.dwDuration - Effect->DiEnvelope.dwFadeTime
                   < Effect->DiEnvelope.dwAttackTime )
                {
                    Effect->DiEnvelope.dwFadeTime = Effect->DiEnvelope.dwAttackTime;
                }
                Effect->DiEffect.lpEnvelope = &Effect->DiEnvelope;
            }

            // TypeSpecificParams を設定する
            Effect->DiEffect.cbTypeSpecificParams = DiEffect->cbTypeSpecificParams;

            // DIEP_TYPESPECIFICPARAMS を設定する
            if( Flags & FFEP_TYPESPECIFICPARAMS )
            {
                if(CFEqual(EffectType, kFFEffectType_CustomForce_ID)) {
                    memcpy(
                           &Effect->DiCustomForce
                           ,DiEffect->lpvTypeSpecificParams
                           ,DiEffect->cbTypeSpecificParams );
                    Effect->DiEffect.lpvTypeSpecificParams = &Effect->DiCustomForce;
                }

                else if(CFEqual(EffectType, kFFEffectType_ConstantForce_ID)) {
                    memcpy(
                           &Effect->DiConstantForce
                           ,DiEffect->lpvTypeSpecificParams
                           ,DiEffect->cbTypeSpecificParams );
                    Effect->DiEffect.lpvTypeSpecificParams = &Effect->DiConstantForce;
                }
                else if(CFEqual(EffectType, kFFEffectType_Square_ID) || CFEqual(EffectType, kFFEffectType_Sine_ID) || CFEqual(EffectType, kFFEffectType_Triangle_ID) || CFEqual(EffectType, kFFEffectType_SawtoothUp_ID) || CFEqual(EffectType, kFFEffectType_SawtoothDown_ID) ) {
                    memcpy(
                           &Effect->DiPeriodic
                           ,DiEffect->lpvTypeSpecificParams
                           ,DiEffect->cbTypeSpecificParams );
                    Effect->DiEffect.lpvTypeSpecificParams = &Effect->DiPeriodic;
                    fprintf(stderr, "Mag: %ud; Dur: %ud\n", Effect->DiPeriodic.dwMagnitude, DiEffect->dwDuration);
                }
                else if(CFEqual(EffectType, kFFEffectType_RampForce_ID)) {
                    memcpy(
                           &Effect->DiRampforce
                           ,DiEffect->lpvTypeSpecificParams
                           ,DiEffect->cbTypeSpecificParams );
                    Effect->DiEffect.lpvTypeSpecificParams = &Effect->DiRampforce;
                }
            }

            // DIEP_STARTDELAY を設定する
            if( Flags & FFEP_STARTDELAY )
            {
                Effect->DiEffect.dwStartDelay = DiEffect->dwStartDelay;
            }

            // DIEP_START が指定されていればエフェクトを再生する
            if( Flags & FFEP_START )
            {
                Effect->Status  = FFEGES_PLAYING;
                Effect->PlayCount = 1;
                Effect->StartTime = CFAbsoluteTimeGetCurrent();
            }

            // DIEP_NORESTART は無視する
            if( Flags & FFEP_NORESTART )
            {
                ;
            }
            Result = FF_OK;
        }
    });
    return( Result );
}

HRESULT Feedback360::GetForceFeedbackState(ForceFeedbackDeviceState *DeviceState)
{
    // デバイス状態のサイズをチェックする
    if( DeviceState->dwSize != sizeof( FFDEVICESTATE ) )
    {
        return( FFERR_INVALIDPARAM );
    }

    dispatch_sync(Queue, ^{

        // エフェクト ドライバの状態を返す
        DeviceState->dwState = NULL;
        // エフェクト リストが空かどうか
        if( EffectCount == 0 )
        {
            DeviceState->dwState |= FFGFFS_EMPTY;
        }
        // エフェクトが停止中かどうか
        if( Stopped == TRUE )
        {
            DeviceState->dwState |= FFGFFS_STOPPED;
        }
        // エフェクトが一時停止中かどうか
        if( Paused == TRUE )
        {
            DeviceState->dwState |= FFGFFS_PAUSED;
        }
        // アクチュエータが作動しているかどうか
        if( Actuator == TRUE )
        {
            DeviceState->dwState |= FFGFFS_ACTUATORSON;
        } else {
            DeviceState->dwState |= FFGFFS_ACTUATORSOFF;
        }
        // 電源はオン固定
        DeviceState->dwState |= FFGFFS_POWERON;
        // セーフティ スイッチはオフ固定
        DeviceState->dwState |= FFGFFS_SAFETYSWITCHOFF;
        // ユーザー スイッチはオン固定
        DeviceState->dwState |= FFGFFS_USERFFSWITCHON;

        // ロード可能数は無制限固定
        DeviceState->dwLoad  = 0;
    });

    return( FF_OK );
}

HRESULT Feedback360::GetForceFeedbackCapabilities(FFCAPABILITIES *capabilities)
{
    capabilities->ffSpecVer.majorRev=kFFPlugInAPIMajorRev;
    capabilities->ffSpecVer.minorAndBugRev=kFFPlugInAPIMinorAndBugRev;
    capabilities->ffSpecVer.stage=kFFPlugInAPIStage;
    capabilities->ffSpecVer.nonRelRev=kFFPlugInAPINonRelRev;
    //capabilities->supportedEffects=FFCAP_ET_SQUARE|FFCAP_ET_SINE|FFCAP_ET_TRIANGLE|FFCAP_ET_SAWTOOTHUP|FFCAP_ET_SAWTOOTHDOWN;
    capabilities->supportedEffects=FFCAP_ET_CUSTOMFORCE|FFCAP_ET_CONSTANTFORCE|FFCAP_ET_RAMPFORCE|FFCAP_ET_SQUARE|FFCAP_ET_SINE|FFCAP_ET_TRIANGLE|FFCAP_ET_SAWTOOTHUP|FFCAP_ET_SAWTOOTHDOWN;
    capabilities->emulatedEffects=0;
    capabilities->subType=FFCAP_ST_VIBRATION;
    capabilities->numFfAxes=2;
    capabilities->ffAxes[0]=FFJOFS_X;
    capabilities->ffAxes[1]=FFJOFS_Y;
    capabilities->storageCapacity=256;
    capabilities->playbackCapacity=1;
    capabilities->driverVer.majorRev=FeedbackDriverVersionMajor;
    capabilities->driverVer.minorAndBugRev=FeedbackDriverVersionMinor;
    capabilities->driverVer.stage=FeedbackDriverVersionStage;
    capabilities->driverVer.nonRelRev=FeedbackDriverVersionNonRelRev;
    capabilities->firmwareVer.majorRev=1;
    capabilities->firmwareVer.minorAndBugRev=0;
    capabilities->firmwareVer.stage=developStage;
    capabilities->firmwareVer.nonRelRev=0;
    capabilities->hardwareVer.majorRev=1;
    capabilities->hardwareVer.minorAndBugRev=0;
    capabilities->hardwareVer.stage=developStage;
    capabilities->hardwareVer.nonRelRev=0;
    return FF_OK;
}

HRESULT Feedback360::SendForceFeedbackCommand(FFCommandFlag state)
{
    // 変数宣言
    __block HRESULT Result = FF_OK;

    // コマンドによって処理を振り分ける
    dispatch_sync(Queue, ^{
        switch( state ) {

            case FFSFFC_RESET:
                // 全てのエフェクトを削除する
                for( LONG Index = 0; Index < EffectCount; Index ++ )
                {
                    delete EffectList[Index];
                }
                // エフェクト数を 0 にする
                EffectCount = 0;
                // エフェクト リストを削除する
                free( EffectList );
                EffectList = NULL;
                // 再生を停止する
                Stopped = TRUE;
                // 一時停止を解除する
                Paused = FALSE;
                break;

            case FFSFFC_STOPALL:
                // 全てのエフェクトの再生を停止する
                for( LONG Index = 0; Index < EffectCount; Index ++ )
                {
                    EffectList[Index]->Status = NULL;
                }
                // 再生を停止する
                Stopped = TRUE;
                // 一時停止を解除する
                Paused = FALSE;
                break;

            case FFSFFC_PAUSE:
                // 再生を一時停止する
                Paused  = TRUE;
                PausedTime = CFAbsoluteTimeGetCurrent();
                break;

            case FFSFFC_CONTINUE:
                // 一時停止を解除する
                for( LONG Index = 0; Index < EffectCount; Index ++ )
                {
                    // 一時停止した時間だけエフェクトの開始時間を遅らせる
                    EffectList[Index]->StartTime += ( CFAbsoluteTimeGetCurrent() - PausedTime );
                }
                Paused = FALSE;
                break;

            case FFSFFC_SETACTUATORSON:
                // アクチュエータを有効にする
                Actuator = TRUE;
                break;

            case FFSFFC_SETACTUATORSOFF:
                // アクチュエータを無効にする
                Actuator = FALSE;
                break;

            default:
                Result = FFERR_INVALIDPARAM;
                break;
        }
    });
    return FF_OK;
}

HRESULT Feedback360::InitializeTerminate(NumVersion APIversion, io_object_t hidDevice, boolean_t begin)
{
    if(begin) {
        if(APIversion.majorRev!=kFFPlugInAPIMajorRev)
        {
            //                fprintf(stderr,"Feedback: Invalid version\n");
            return FFERR_INVALIDPARAM;
        }
        // From probe
        if( (hidDevice==0)
           || ((!IOObjectConformsTo(hidDevice,"Xbox360ControllerClass"))
               &&  (!IOObjectConformsTo(hidDevice,"Wireless360Controller"))) )
        {
            //                fprintf(stderr,"Feedback: Invalid device\n");
            return FFERR_INVALIDPARAM;
        }
        if(!Device_Initialise(&this->device, hidDevice)) {
            //               fprintf(stderr,"Feedback: Failed to initialise\n");
            return FFERR_NOINTERFACE;
        }
        Queue = dispatch_queue_create("com.mice.driver.Feedback360", NULL);
        Timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, Queue);
        dispatch_source_set_timer(Timer, dispatch_walltime(NULL, 0), LoopGranularity*1000, 10);
        dispatch_set_context(Timer, this);
        dispatch_source_set_event_handler_f(Timer, EffectProc);
        dispatch_resume(Timer);
    }
    else {
        dispatch_sync(Queue, ^{
            dispatch_source_cancel(Timer);
            SetForce(0, 0);
            Device_Finalise(&this->device);
        });

    }
    return FF_OK;
}

HRESULT Feedback360::DestroyEffect(FFEffectDownloadID EffectHandle)
{
    __block HRESULT Result = FF_OK;
    dispatch_sync(Queue, ^{

    // ハンドルからエフェクトを検索する
    for( LONG Index = 0; Index < EffectCount; Index ++ )
    {
        if( EffectList[Index]->Handle == EffectHandle )
        {
            // エフェクトを削除する
            delete EffectList[Index];
            // エフェクト数を減らす
            EffectCount --;
            if( EffectCount > 0 )
            {
                // エフェクト リストを縮小する
                memcpy(
                       &EffectList[Index]
                       ,&EffectList[Index + 1]
                       ,sizeof( Feedback360Effect * ) * ( EffectCount - Index ) );
                Feedback360Effect **NewEffectList;
                NewEffectList = (Feedback360Effect * *)realloc( EffectList, sizeof( Feedback360Effect * ) * EffectCount );
                if( NewEffectList != NULL )
                {
                    EffectList = NewEffectList;
                } else {
                    // エラーを返す
                    Result = E_OUTOFMEMORY;
                }
            } else {
                // エフェクト リストを削除する
                free( EffectList );
                EffectList = NULL;
            }
            break;
        }
    }
    });
    return( Result );

}

HRESULT Feedback360::Escape(FFEffectDownloadID downloadID, FFEFFESCAPE *escape)
{
    if(downloadID!=0) return FFERR_UNSUPPORTED;
    if(escape->dwSize<sizeof(FFEFFESCAPE)) return FFERR_INVALIDPARAM;
    escape->cbOutBuffer=0;
    switch(escape->dwCommand) {
        case 0x00:  // Control motors
            if(escape->cbInBuffer!=1) return FFERR_INVALIDPARAM;
            dispatch_sync(Queue, ^{
                Manual=((unsigned char*)escape->lpvInBuffer)[0]!=0x00;
            });
            break;
        case 0x01:  // Set motors
            if(escape->cbInBuffer!=2) return FFERR_INVALIDPARAM;
            dispatch_sync(Queue, ^{
            if(Manual) {
                unsigned char *data=(unsigned char *)escape->lpvInBuffer;
                unsigned char buf[]={0x00,0x04,data[0],data[1]};
                Device_Send(&this->device,buf,sizeof(buf));
            }
            });
            break;
        case 0x02:  // Set LED
            if(escape->cbInBuffer!=1) return FFERR_INVALIDPARAM;
        {
            dispatch_sync(Queue, ^{
            unsigned char *data=(unsigned char *)escape->lpvInBuffer;
            unsigned char buf[]={0x01,0x03,data[0]};
            Device_Send(&this->device,buf,sizeof(buf));
            });
        }
            break;
        case 0x03:  // Power off
        {
            dispatch_sync(Queue, ^{
            unsigned char buf[] = {0x02, 0x02};
            Device_Send(&this->device, buf, sizeof(buf));
            });
        }
            break;
        default:
            fprintf(stderr, "Xbox360Controller FF plugin: Unknown escape (%i)\n", (int)escape->dwCommand);
            return FFERR_UNSUPPORTED;
    }
    return FF_OK;

}

void Feedback360::SetForce(LONG LeftLevel, LONG RightLevel)
{
    fprintf(stderr, "LS: %d; RS: %d\n", (unsigned char)MIN( 255, LeftLevel * Gain / 10000 ), (unsigned char)MIN( 255, RightLevel * Gain / 10000 ));
    unsigned char buf[]={0x00,0x04,(unsigned char)MIN( 255, LeftLevel * Gain / 10000 ),(unsigned char)MIN( 255, RightLevel * Gain / 10000 )};
    if(!Manual) Device_Send(&device,buf,sizeof(buf));
}

void Feedback360::EffectProc( void *params )
{
    Feedback360 *cThis = (Feedback360 *)params;

    //dispatch_sync(cThis->Queue, ^{
    LONG LeftLevel = 0;
    LONG RightLevel = 0;
    LONG Gain  = cThis->Gain;

    //cThis = *Feedback360::getThis(params);
    //fprintf(stderr, "EC2: %d\n", cThis->EffectCount);

    if( cThis->Actuator == TRUE )
    {
        // エフェクトの強さを計算する
        for(  int Index = 0; Index < cThis->EffectCount; Index ++ )
        {
            cThis->EffectList[Index]->Calc( &LeftLevel, &RightLevel );
        }
    }

    //fprintf(stderr, "Actuator: %d, L: %d, R: %d\n", cThis->Actuator, LeftLevel, RightLevel);


    // コントローラーの振動を設定する
    if( cThis->PrvLeftLevel != LeftLevel || cThis->PrvRightLevel != RightLevel )
    {
        fprintf(stderr, "PL: %d, PR: %d; L: %d, R: %d; \n", cThis->PrvLeftLevel, cThis->PrvRightLevel, LeftLevel, RightLevel);
        cThis->SetForce((unsigned char)MIN(255, LeftLevel * Gain / 10000),(unsigned char)MIN( 255, RightLevel * Gain / 10000 ));
    }

    // エフェクトの強さを退避する
    cThis->PrvLeftLevel = LeftLevel;
    cThis->PrvRightLevel = RightLevel;
    //});

}

HRESULT Feedback360::GetEffectStatus(FFEffectDownloadID EffectHandle, FFEffectStatusFlag *Status)
{
    // ハンドルからエフェクトを検索する
    dispatch_sync(Queue, ^{
    for( LONG Index = 0; Index < EffectCount; Index ++ )
    {
        if( EffectList[Index]->Handle == EffectHandle )
        {
            // エフェクトの状態を返す
            *Status = EffectList[Index]->Status;
            break;
        }
    }
    });
    return( FF_OK );

}

HRESULT Feedback360::GetVersion(ForceFeedbackVersion *version)
{
    version->apiVersion.majorRev=kFFPlugInAPIMajorRev;
    version->apiVersion.minorAndBugRev=kFFPlugInAPIMinorAndBugRev;
    version->apiVersion.stage=kFFPlugInAPIStage;
    version->apiVersion.nonRelRev=kFFPlugInAPINonRelRev;
    version->plugInVersion.majorRev=FeedbackDriverVersionMajor;
    version->plugInVersion.minorAndBugRev=FeedbackDriverVersionMinor;
    version->plugInVersion.stage=FeedbackDriverVersionStage;
    version->plugInVersion.nonRelRev=FeedbackDriverVersionNonRelRev;
    return FF_OK;
}


// static c->c++ glue functions

HRESULT Feedback360::sQueryInterface(void *self, REFIID iid, LPVOID *ppv) {
    Feedback360 *obj = ( (Xbox360InterfaceMap *) self)->obj;
    return obj->QueryInterface(iid, ppv);
}
ULONG Feedback360::sAddRef(void *self) {
    Feedback360 *obj = ( (Xbox360InterfaceMap *) self)->obj;
    return obj->AddRef();
}
ULONG Feedback360::sRelease(void *self) {
    Feedback360 *obj = ( (Xbox360InterfaceMap *) self)->obj;
    return obj->Release();
}
IOReturn Feedback360::sProbe(void *self, CFDictionaryRef propertyTable, io_service_t service, SInt32 *order) {
    return getThis(self)->Probe(propertyTable, service, order);
}
IOReturn Feedback360::sStart(void *self, CFDictionaryRef propertyTable, io_service_t service) {
    return getThis(self)->Start(propertyTable, service);
}
IOReturn Feedback360::sStop(void *self) {
    return getThis(self)->Stop();
}
HRESULT Feedback360::sGetVersion(void * self, ForceFeedbackVersion * version) {
    return Feedback360::getThis(self)->GetVersion(version);
}
HRESULT Feedback360::sInitializeTerminate(void * self, NumVersion forceFeedbackAPIVersion, io_object_t hidDevice, boolean_t begin ) {
    return Feedback360::getThis(self)->InitializeTerminate(forceFeedbackAPIVersion, hidDevice, begin);
}
HRESULT Feedback360::sDestroyEffect(void * self, FFEffectDownloadID downloadID) {
    return Feedback360::getThis(self)->DestroyEffect(downloadID);
}
HRESULT Feedback360::sDownloadEffect(void * self, CFUUIDRef effectType, FFEffectDownloadID *pDownloadID, FFEFFECT * pEffect, FFEffectParameterFlag flags ) {
    return Feedback360::getThis(self)->DownloadEffect(effectType, pDownloadID, pEffect, flags);
}
HRESULT Feedback360::sEscape( void * self, FFEffectDownloadID downloadID, FFEFFESCAPE * pEscape) {
    return Feedback360::getThis(self)->Escape(downloadID, pEscape);
}
HRESULT Feedback360::sGetEffectStatus( void * self, FFEffectDownloadID downloadID, FFEffectStatusFlag * pStatusCode ) {
    return Feedback360::getThis(self)->GetEffectStatus(downloadID, pStatusCode);
}
HRESULT Feedback360::sGetForceFeedbackState( void * self, ForceFeedbackDeviceState * pDeviceState ) {
    return Feedback360::getThis(self)->GetForceFeedbackState(pDeviceState);
}
HRESULT Feedback360::sGetForceFeedbackCapabilities( void * self, FFCAPABILITIES * capabilities ) {
    return Feedback360::getThis(self)->GetForceFeedbackCapabilities(capabilities);
}

HRESULT Feedback360::sSendForceFeedbackCommand( void * self, FFCommandFlag state ) {
    return Feedback360::getThis(self)->SendForceFeedbackCommand(state);
}
HRESULT Feedback360::sSetProperty( void * self, FFProperty property, void * pValue ) {
    return Feedback360::getThis(self)->SetProperty(property, pValue);
}
HRESULT Feedback360::sStartEffect( void * self, FFEffectDownloadID downloadID, FFEffectStartFlag mode, UInt32 iterations ) {
    return Feedback360::getThis(self)->StartEffect(downloadID, mode, iterations);
}
HRESULT Feedback360::sStopEffect( void * self, UInt32 downloadID ) {
    return Feedback360::getThis(self)->StopEffect(downloadID);
}



// External factory function

extern "C" void* Control360Factory(CFAllocatorRef allocator,CFUUIDRef typeID)
{
    void* result = NULL;
    if(CFEqual(typeID,kIOForceFeedbackLibTypeID))
        result = (void*)Feedback360::Alloc();
    return (void*)result;
}
