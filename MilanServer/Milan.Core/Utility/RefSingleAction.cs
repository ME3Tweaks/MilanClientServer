namespace Milan.Core.Utility;

public delegate void RefSingleAction<T>(ref T param) where T : allows ref struct;
