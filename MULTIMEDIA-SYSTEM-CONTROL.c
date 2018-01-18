/*
 * Copyright (C) 1997-2009 by Objective Systems, Inc.
 *
 * This software is furnished under an open source license and may be
 * used and copied only in accordance with the terms of this license.
 * The text of the license may generally be found in the root
 * directory of this installation in the COPYING file.  It
 * can also be viewed online at the following URL:
 *
 *   http://www.obj-sys.com/open/license.html
 *
 * Any redistributions of this file including modified versions must
 * maintain this copyright notice.
 *
 *****************************************************************************/
/**
 * This file was generated by the Objective Systems ASN1C Compiler
 * (http://www.obj-sys.com).  Version: 6.2.B, Date: 06-Apr-2009.
 */
#include "MULTIMEDIA-SYSTEM-CONTROL.h"
#include "ooCommon.h"

EXTERN const char* gs_MULTIMEDIA_SYSTEM_CONTROL_NetworkAccessParameters_networkAddress_e164Address_CharSet =
"#*,0123456789";

EXTERN const char* gs_MULTIMEDIA_SYSTEM_CONTROL_UserInputIndication_signal_signalType_CharSet =
"!#*0123456789ABCD";


static char *H245_AUDIO_CAPACITY_NAMES[] = {
        "T_H245AudioCapability_0"
        "T_H245AudioCapability_nonStandard",
        "T_H245AudioCapability_g711Alaw64k",
        "T_H245AudioCapability_g711Alaw56k",
        "T_H245AudioCapability_g711Ulaw64k",
        "T_H245AudioCapability_g711Ulaw56k",
        "T_H245AudioCapability_g722_64k",
        "T_H245AudioCapability_g722_56k",
        "T_H245AudioCapability_g722_48k",
        "T_H245AudioCapability_g7231",
        "T_H245AudioCapability_g728",
        "T_H245AudioCapability_g729",
        "T_H245AudioCapability_g729AnnexA",
        "T_H245AudioCapability_is11172AudioCapability",
        "T_H245AudioCapability_is13818AudioCapability",
        "T_H245AudioCapability_g729wAnnexB",
        "T_H245AudioCapability_g729AnnexAwAnnexB",
        "T_H245AudioCapability_g7231AnnexCCapability",
        "T_H245AudioCapability_gsmFullRate",
        "T_H245AudioCapability_gsmHalfRate",
        "T_H245AudioCapability_gsmEnhancedFullRate",
        "T_H245AudioCapability_genericAudioCapability",
        "T_H245AudioCapability_g729Extensions",
        "T_H245AudioCapability_vbd",
        "T_H245AudioCapability_audioTelephonyEvent",
        "T_H245AudioCapability_audioTone",
        "T_H245AudioCapability_extElem1"
};

const char *ooH245AudioCapText(int t)
{
        if (t > sizeof(H245_AUDIO_CAPACITY_NAMES)) return "OVERFLOW";
        return H245_AUDIO_CAPACITY_NAMES[t];
}


static char *H245_VIDEO_CAPACITY_NAMES[] = {
        "T_H245VideoCapability_0",
        "T_H245VideoCapability_nonStandard",
        "T_H245VideoCapability_h261VideoCapability",
        "T_H245VideoCapability_h262VideoCapability",
        "T_H245VideoCapability_h263VideoCapability",
        "T_H245VideoCapability_is11172VideoCapability",
        "T_H245VideoCapability_genericVideoCapability",
        "T_H245VideoCapability_extendedVideoCapability",
        "T_H245VideoCapability_extElem1"
};

const char *ooH245VideoCapText(int t)
{
        if (t > sizeof(H245_VIDEO_CAPACITY_NAMES)) return "OVERFLOW";
        return H245_VIDEO_CAPACITY_NAMES[t];
}