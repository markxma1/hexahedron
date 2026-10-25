package com.xma1.hexahedron;

import org.libsdl.app.SDLActivity;

// Everything runs in native code (libmain.so); SDL's activity does the rest.
public class HexahedronActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }
}
