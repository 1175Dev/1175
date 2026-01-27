package org.elevenseventyfivecore.qt;

import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;

import org.qtproject.qt5.android.bindings.QtActivity;

import java.io.File;

public class ElevenSeventyFiveQtActivity extends QtActivity
{
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        final File elevenseventyfiveDir = new File(getFilesDir().getAbsolutePath() + "/.elevenseventyfive");
        if (!elevenseventyfiveDir.exists()) {
            elevenseventyfiveDir.mkdir();
        }

        super.onCreate(savedInstanceState);
    }
}
