struct vs_out {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

vs_out main( float4 pos : POSITION, float4 color : COLOR) {
    vs_out output;

    output.position = pos;
    output.color = color;

	return output;
}