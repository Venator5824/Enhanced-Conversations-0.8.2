every single .onnx model you use must have an EC_Voices_List_UniqueNumber.ini file
.
You for now, version 0.8.0, 0.8.1, 0.8.2, and 0.8.3, need VITS model versions that work with pyper.
Using: babylon.cpp, phonemizer
the .dll will get all that got the format
EC_Voices_List_01.ini
up until
EC_Voices_List_99.ini
for now.

Its normal dezimal numbering. 

just create one, and write
model_name =  model.onnx
on top. 
only these models will be loaded
if you got internal voices numbered, do it like this: 

 formula: 
; [ID NUMBER 1 to X]
; rep internal id: the id named internally when given
; gender: female f, male m, neutral n (animals, neutral, ..) mandatory
; age: young y, middle m, old o - optional
; voice: l low, n normal, h high - optional
; special: special infos - optional


. if you go no, the default voice will be used.
in the personas.ini, do the following to hardset a person his voice usage:
voice_model = model
(no .onnx!)
voice_id = number or empty when none rquired, the ID number, not internal ID
; internal ID is when its internally an different ID than in your list, the rep internal id will ber prefered. 
like this:

[ID 1]
rep_internal_id: 225
gender: f
age: y
voice: n
special: English, Southern England

. you can set the default fallback .onnx in the settings.ini.
like TTS_Model_alternative_name = modelbase.onnx
and then here you do for your main cahracter, sophia:

an EC_Voices_list_03.ini and edit it and write:
model_name = outputsohpia.onnx
. 
and then you got to your npc in the settings and write: 
[npc hash]
name, traits, ....
voice_model = outputsophia
voice_id = 
none or integer without hashes - decimal


PATHS:
voices ini files:
root/ECModels/TTSData
; hardcoded
model:
root/ECModels/modelfolder/model
or your custom path settable
main .asi or .dll:
root/
setting files:
root/
or 
roo/ECmodels/BaseData
