RWTexture3D<float>		rwSdf		: register(u0);

[numthreads(1, 1, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
	// �K���ɑ傫�Ȓl�ŃN���A
	// SDF�̓o�E���f�B���O�{�b�N�X�̑傫���Ő��K�������̂ŁA1.0�ȏ�Ȃ�OK
	rwSdf[dispatchID] = 10000.0;
}