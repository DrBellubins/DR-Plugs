#pragma once

class PMath
{
    public:

    static float Lerp(float StartValue, float EndValue, float Amount01)
    {
        return StartValue + (EndValue - StartValue) * Amount01;
    }
};