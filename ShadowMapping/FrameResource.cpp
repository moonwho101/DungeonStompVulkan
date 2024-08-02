#include "FrameResource.h"


FrameResource::FrameResource(PassConstants* pc, ObjectConstants* oc, MaterialData* md, Vertex* pWvs) {
	pPCs = pc;
	pOCs = oc;
	pMats = md;
	pWavesVB = pWvs;
}

FrameResource::~FrameResource() {

}