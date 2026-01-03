// ECStartup.cs
// For versions 0.7.0 or higher of Enhanced Conversations
// place this in your folder and then start the game
// Press F10 to check for most important calls
// open - you can code whatever you want, as long as its not: A) offensive or harmfull in real life, B) harmfull against third parties or third parties rights, C) similar

// For GTA V: 
using System;
using System.Runtime.InteropServices;
using GTA;
using GTA.Math;
using GTA.Native;
using System.Windows.Forms;

public class ECCheck : Script
{
    // IMPORT C++ EXPORTS

    // this name matches the .asi file, the main script file you need to play the game
    // an entity here refers to an living NPC that is a human
    
    const string DLL_NAME = "GTA_EC_01.asi"; 

    [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool API_IsModReady();

    [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void API_SetEntityIdentity(int pedHandle, string name, string gender);

    [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void API_SetEntityGoal(int pedHandle, string goal);

    [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern void API_AddEntityMemory(int pedHandle, string fact);

    [DllImport(DLL_NAME, CallingConvention = CallingConvention.Cdecl)]
    public static extern bool API_HasEntityBrain(int pedHandle);

    public ECCheck()
    {
        KeyDown += OnKeyDown;
    }

    private void OnKeyDown(object sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.F10)
        {
            RunSystemCheck();
        }
    }

    private void RunSystemCheck()
    {
        GTA.UI.Notification.Show("Starting ECCheck...");

        try
        {
            bool ready = API_IsModReady();
            string statusColor = ready ? "~g~OK" : "~y~WAITING";
            GTA.UI.Notification.Show("[1/4] DLL Link: " + statusColor);
            if (!ready) return;

            Ped target = World.GetClosestPed(Game.Player.Character.Position, 10.0f);
            if (target == null)
            {
                GTA.UI.Notification.Show("~y~No NPC nearby.");
                return;
            }

            // Teste Setter
            API_SetEntityIdentity(target.Handle, "Subject Alpha", "Male");
            GTA.UI.Notification.Show("[2/4] SetIdentity: ~g~OK");

            API_SetEntityGoal(target.Handle, "Follow the test protocol.");
            GTA.UI.Notification.Show("[3/4] SetGoal: ~g~OK");

            // Prüfe Brain
            bool hasBrain = API_HasEntityBrain(target.Handle);
            if (hasBrain)
            {
                GTA.UI.Notification.Show("[4/4] Brain Check: ~g~SUCCESS");
                GTA.UI.Screen.ShowSubtitle("~g~SYSTEM CHECK PASSED!~w~ Entity: " + target.Handle.ToString(), 5000);
            }
            else
            {
                GTA.UI.Notification.Show("[4/4] Brain Check: ~r~FAILED");
            }
        }
        catch (Exception ex)
        {
            GTA.UI.Notification.Show("~r~ERROR: " + ex.Message);
        }
    }
}

// EOF //
