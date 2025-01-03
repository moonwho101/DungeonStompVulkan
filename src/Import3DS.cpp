//-----------------------------------------------------------------------------
// File: Import3DS.cpp
//
// Desc: Code for importing 3D Studio models
// Copyright (c) 1999 William Chin. All rights reserved.
//-----------------------------------------------------------------------------

#include <windows.h>
#include <stdio.h>
#include <math.h>
#include "world.hpp"
#include "Import3DS.hpp"
#include "LoadWorld.hpp"

OBJECT3DS oblistitem[MAX_NUM_3DS_OBJECTS];

int kf_count = 0;

short pt_flags;
short unknown;
unsigned long kfstart, kfend;
unsigned long kfcurframe;

int total_num_objects;
char object_names[MAX_NUM_3DS_OBJECTS][MAX_NAME_LENGTH];
BOOL command_found_flag;
BOOL loading_first_model_flag = TRUE;
BOOL bEnable3dsLogfile = FALSE;

int num_materials = 0;
int num_maps = 0;
int total_num_faces;
int total_num_verts;
int last_num_verts;
int last_num_faces;
int last_num_mcoords;
int total_num_mcoords;
int total_num_frames = 1;

FACE3DS faces[MAX_NUM_3DS_FACES];
float fverts[MAX_NUM_3DS_VERTICES][3];
MAPPING_COORDINATES mcoords[MAX_NUM_3DS_VERTICES];
int object_texture[MAX_NUM_3DS_OBJECTS];

char material_list[MAX_NUM_3DS_TEXTURES][MAX_NAME_LENGTH];
char mapnames[MAX_NUM_3DS_TEXTURES][MAX_NAME_LENGTH];

int num_verts_in_object[MAX_NUM_3DS_OBJECTS];
int num_faces_in_object[MAX_NUM_3DS_OBJECTS];

FILE* logfile;
FILE* fp_3dsmodel;

BOOL Import3DS(char* filename, int pmodel_id, float scale)
{
	FILE* fp;

	int done;
	int i, j, cnt;
	int length;
	int data_length;
	int frame_num;
	int num_frames;
	int datfile_vert_cnt;
	int quad_cnt = 0;
	unsigned short command;
	char temp;
	float tx, ty, tz;
	float angle;
	float x, y, z;
	BOOL process_data_flag;
	char datfilename[1024];
	int file_ex_start = 0;

	total_num_objects = -1;
	kf_count = -1;
	total_num_faces = 0;
	total_num_verts = 0;
	last_num_faces = 0;
	last_num_verts = 0;
	num_maps = 0;
	num_materials = 0;
	last_num_mcoords = 0;
	total_num_mcoords = 0;
	num_materials = 0;

	if (fopen_s(&fp, filename, "rb") != 0)
	{
		//MessageBox(hwnd, "3DS File error : Can't open", filename, MB_OK);
		return FALSE;
	}
	done = 0;

	while (done == 0)
	{
		process_data_flag = FALSE;
		command_found_flag = FALSE;

		fread(&command, sizeof(command), 1, fp);

		// Test for end of file
		if (feof(fp))
		{
			done = 1;
			break;
		}

		fread(&length, sizeof(length), 1, fp);
		data_length = length - 6;

		// Process 3DS file commands
		switch (command)
		{
		case TRIANGLE_MESH:
			//PrintLogFile(logfile, "TRIANGLE_MESH");
			process_data_flag = TRUE;
			break;

		case TRIANGLE_VERTEXLIST:
			if (ProcessVertexData(fp) == TRUE)
				process_data_flag = TRUE;
			else
			{
				//MessageBox(hwnd, "Error : Too many verts", NULL, MB_OK);
				return FALSE;
			}
			break;

		case TRIANGLE_FACELIST:
			if (ProcessFaceData(fp) == TRUE)
				process_data_flag = TRUE;
			else
			{
				//MessageBox(hwnd, "Error : Too many faces", NULL, MB_OK);
				return FALSE;
			}
			break;

		case TRIANGLE_MAPPINGCOORS:
			ProcessMappingData(fp);
			process_data_flag = TRUE;
			break;

		case TRIANGLE_MATERIAL:
			ProcessMaterialData(fp, pmodel_id);
			process_data_flag = TRUE;
			break;

		case EDIT_MATERIAL:
			//PrintLogFile(logfile, "\nEDIT_MATERIAL");
			process_data_flag = TRUE;
			break;

		case MAT_NAME01:
			AddMaterialName(fp);
			process_data_flag = TRUE;
			break;

		case TEXTURE_MAP:
			//PrintLogFile(logfile, "TEXTURE_MAP");
			process_data_flag = TRUE;
			break;

		case MAPPING_NAME:
			AddMapName(fp, pmodel_id);
			process_data_flag = TRUE;
			break;

		case NAMED_OBJECT:

			// read name and store
			total_num_objects++;

			for (i = 0; i < MAX_NAME_LENGTH; i++)
			{
				fread(&temp, 1, 1, fp);

				if (total_num_objects >= MAX_NUM_3DS_OBJECTS)
					return FALSE;

				object_names[total_num_objects][i] = temp;
				data_length--;

				if (temp == 0)
				{
					if (bEnable3dsLogfile)
					{
						fprintf(logfile, "\n\n%s %s\n", "NAMED_OBJECT ",
							object_names[total_num_objects]);
					}

					break;
				}
			}

			process_data_flag = TRUE;
			break;

		case MAIN3DS:
			//PrintLogFile(logfile, "MAIN3DS");
			process_data_flag = TRUE;
			break;

		case EDIT3DS:
			//PrintLogFile(logfile, "EDIT3DS");
			process_data_flag = TRUE;
			break;

		case KEYFRAME:
			//PrintLogFile(logfile, "\n\nKEYFRAME");
			process_data_flag = TRUE;
			break;

		case KEYFRAME_MESH_INFO:
			//PrintLogFile(logfile, "\nKEYFRAME_MESH_INFO");
			kf_count++;
			process_data_flag = TRUE;
			break;

		case KEYFRAME_START_AND_END:
			//PrintLogFile(logfile, "KEYFRAME_START_AND_END");
			fread(&kfstart, sizeof(unsigned long), 1, fp);
			fread(&kfend, sizeof(unsigned long), 1, fp);
			process_data_flag = TRUE;
			break;

		case KEYFRAME_HEADER:
			//PrintLogFile(logfile, "KEYFRAME_HEADER");
			//process_data_flag = TRUE;
			break;

		case KFCURTIME:
			fread(&kfcurframe, sizeof(long), 1, fp);
			if (bEnable3dsLogfile)
				fprintf(logfile, "KFCURTIME  %d\n", kfcurframe);
			process_data_flag = TRUE;
			break;

		case PIVOT:
			ProcessPivots(fp);
			process_data_flag = TRUE;
			break;

		case POS_TRACK_TAG:
			ProcessPositionTrack(fp);
			process_data_flag = TRUE;
			break;

		case ROT_TRACK_TAG:
			ProcessRotationTrack(fp);
			process_data_flag = TRUE;
			break;

		case SCL_TRACK_TAG:
			ProcessScaleTrack(fp);
			process_data_flag = TRUE;
			break;

		case FOV_TRACK_TAG:
			//PrintLogFile(logfile, "PIVOT");
			//process_data_flag = TRUE;
			break;

		case ROLL_TRACK_TAG:
			//PrintLogFile(logfile, "PIVOT");
			//process_data_flag = TRUE;
			break;

		case COL_TRACK_TAG:
			//PrintLogFile(logfile, "PIVOT");
			//process_data_flag = TRUE;
			break;

		case MORPH_TRACK_TAG:
			//PrintLogFile(logfile, "PIVOT");
			//process_data_flag = TRUE;
			break;

		case HOT_TRACK_TAG:
			//PrintLogFile(logfile, "PIVOT");
			//process_data_flag = TRUE;
			break;

		case FALL_TRACK_TAG:
			//PrintLogFile(logfile, "PIVOT");
			//process_data_flag = TRUE;
			break;

		case HIDE_TRACK_TAG:
			//PrintLogFile(logfile, "PIVOT");
			//process_data_flag = TRUE;
			break;

		case NODE_HDR:
			ProcessNodeHeader(fp);
			process_data_flag = TRUE;
			break;

		case NODE_ID:
			ProcessNodeId(fp);
			process_data_flag = TRUE;
			break;

		case M3DS_VERSION:
			Process3DSVersion(fp);
			process_data_flag = TRUE;
			break;

		case MESH_VERSION:
			//PrintLogFile(logfile, "MESH_VERSION");
			break;

		case INT_PERCENTAGE:
			//PrintLogFile(logfile, "INT_PERCENTAGE");
			break;

		case MASTER_SCALE:
			ProcessMasterScale(fp);
			process_data_flag = TRUE;
			break;

		case TRIANGLE_MAPPINGSTANDARD:
			//PrintLogFile(logfile, "TRIANGLE_MAPPINGSTANDARD");
			break;

		case TRIANGLE_VERTEXOPTIONS:
			//PrintLogFile(logfile, "TRIANGLE_VERTEXOPTIONS");
			break;

		case TRIANGLE_SMOOTH:
			//PrintLogFile(logfile, "TRIANGLE_SMOOTH");
			ProcessTriSmoothData(fp);
			process_data_flag = TRUE;
			break;

		case TRI_LOCAL:
			//PrintLogFile(logfile, "TRI_LOCAL");
			ProcessTriLocalData(fp);
			process_data_flag = TRUE;
			break;

			// Skipping these commands / chunks
		case TRI_VISIBLE:
			//PrintLogFile(logfile, "TRI_VISIBLE");
			break;

		case MATERIAL_AMBIENT:
			//PrintLogFile(logfile, "MATERIAL_AMBIENT");
			break;

		case MATERIAL_DIFFUSE:
			//PrintLogFile(logfile, "MATERIAL_DIFFUSE");
			break;

		case MATERIAL_SPECULAR:
			//PrintLogFile(logfile, "MATERIAL_SPECULAR");
			break;

		case MATERIAL_SHININESS:
			//PrintLogFile(logfile, "MATERIAL_SHININESS");
			break;

		case MATERIAL_SHIN_STRENGTH:
			//PrintLogFile(logfile, "MATERIAL_SHIN_STRENGTH");
			break;

		case MAPPING_PARAMETERS:
			//PrintLogFile(logfile, "MAPPING_PARAMETERS");
			break;

		case BLUR_PERCENTAGE:
			//PrintLogFile(logfile, "BLUR_PERCENTAGE");
			break;

		case TRANS_PERCENT:
			//PrintLogFile(logfile, "TRANS_PERCENT");
			break;

		case TRANS_FALLOFF_PERCENT:
			//PrintLogFile(logfile, "TRANS_FALLOFF_PERCENT");
			break;

		case REFLECTION_BLUR_PER:
			//PrintLogFile(logfile, "REFLECTION_BLUR_PER");
			break;

		case RENDER_TYPE:
			//PrintLogFile(logfile, "RENDER_TYPE");
			break;

		case SELF_ILLUM:
			//PrintLogFile(logfile, "SELF_ILLUM");
			break;

		case WIRE_THICKNESS:
			//PrintLogFile(logfile, "WIRE_THICKNESS");
			break;

		case IN_TRANC:
			//PrintLogFile(logfile, "IN_TRANC");
			break;

		case SOFTEN:
			//PrintLogFile(logfile, "SOFTEN");
			break;

			break;
		} // end switch

		if (process_data_flag == FALSE)
		{
			if (command_found_flag == FALSE)
			{
				if (bEnable3dsLogfile)
				{
					fprintf(logfile, "\n");
					fprintf(logfile, "%s  %x\n", "UNKNOWN COMMAND ", command);
				}
			}
			// command was unrecognised, so skip it's data
			for (i = 0; i < data_length; i++)
			{
				fread(&temp, 1, 1, fp);
			}
			data_length = 0;
		}

	} // end while

	fclose(fp);

	total_num_objects++;

	// Transfer triangle list into the pmdata structure
	// allocate memory dynamically
	num_frames = MAX_NUM_3DS_FRAMES;

	pmdata[pmodel_id].w = new VERT * [num_frames];

	for (i = 0; i < num_frames; i++)
		pmdata[pmodel_id].w[i] = new VERT[total_num_verts];

	pmdata[pmodel_id].f = new int[total_num_faces * 4];
	pmdata[pmodel_id].num_verts_per_object = new int[total_num_objects];
	pmdata[pmodel_id].num_faces_per_object = new int[total_num_objects];
	pmdata[pmodel_id].poly_cmd = new D3DPRIMITIVETYPE[total_num_objects];
	pmdata[pmodel_id].texture_list = new int[total_num_objects];
	pmdata[pmodel_id].t = new VERT[total_num_faces * 4];

	int mem = 0;
	mem += (sizeof(VERT) * total_num_verts * num_frames);
	mem += (sizeof(int) * total_num_faces * 4);
	mem += (sizeof(int) * total_num_objects);
	mem += (sizeof(int) * total_num_objects);
	mem += (sizeof(D3DPRIMITIVETYPE) * total_num_objects);
	mem += (sizeof(int) * total_num_objects);
	mem += (sizeof(VERT) * total_num_faces * 4);
	mem = mem / 1024;

	strcpy_s(datfilename, filename);

	for (i = 0; i < 1024; i++)
	{
		if (datfilename[i] == '.')
		{
			if (datfilename[i + 1] == '.')
			{
				i++;
			}
			else
			{
				file_ex_start = i;
				break;
			}
		}
	}

	if (file_ex_start == 0)
	{
		//MessageBox(hwnd, "datfilename error :", NULL, MB_OK);
		//return FALSE;
	}

	datfile_vert_cnt = 0;
	frame_num = 0;

	int v, pos_keys, v_start, v_end;

	for (i = 0; i < total_num_objects; i++)
	{
		v_start = oblistitem[i].verts_start;
		v_end = oblistitem[i].verts_end;

		for (v = v_start; v <= v_end; v++)
		{
			fverts[v][0] -= oblistitem[i].pivot.x;
			fverts[v][1] -= oblistitem[i].pivot.y;
			fverts[v][2] -= oblistitem[i].pivot.z;
		}
	}

	// copy verts into pmdata for all frames
	for (frame_num = 0; frame_num < total_num_frames; frame_num++)
	{

		// fix this part swapped 0 1 2 to 0 2 1 see below
		//should be  1 0 2
		for (i = 0; i < total_num_verts; i++)
		{
			pmdata[pmodel_id].w[frame_num][i].x = fverts[i][1];
			pmdata[pmodel_id].w[frame_num][i].y = fverts[i][0];
			pmdata[pmodel_id].w[frame_num][i].z = fverts[i][2];
		}
	}

	// Apply position track data
	float pos_x, pos_y, pos_z;

	if (bEnable3dsLogfile)
		fprintf(logfile, "APPLYING POSITION TRACK DATA\n\n");

	for (i = 0; i < total_num_objects; i++)
	{
		v_start = oblistitem[i].verts_start;
		v_end = oblistitem[i].verts_end;
		pos_keys = oblistitem[i].poskeys;

		for (j = 0; j < pos_keys; j++)
		{
			frame_num = oblistitem[i].pos_track[j].framenum;

			if ((j < total_num_frames) && (frame_num < total_num_frames))
			{
				pos_x = oblistitem[i].pos_track[j].pos_x - oblistitem[i].local_centre_x;
				pos_y = oblistitem[i].pos_track[j].pos_y - oblistitem[i].local_centre_y;
				pos_z = oblistitem[i].pos_track[j].pos_z - oblistitem[i].local_centre_z;

				for (v = v_start; v <= v_end; v++)
				{
					pmdata[pmodel_id].w[frame_num][v].x += pos_x;
					pmdata[pmodel_id].w[frame_num][v].y += pos_y;
					pmdata[pmodel_id].w[frame_num][v].z += pos_z;
					if (bEnable3dsLogfile)
						fprintf(logfile, "obj: %d  frame_num: %d  v: %d\n", i, frame_num, v);
				}
				if (bEnable3dsLogfile)
					fprintf(logfile, "\n");
			}
		}
	}

	if (bEnable3dsLogfile)
		fprintf(logfile, "\n\nAPPLYING ROTATION TRACK DATA\n\n");

	// Apply rotation track data
	int rot_keys;
	float axis_x, axis_y, axis_z, rot_angle;
	float rot_angle_x, rot_angle_y, rot_angle_z;

	for (i = 0; i < total_num_objects; i++)
	{
		v_start = oblistitem[i].verts_start;
		v_end = oblistitem[i].verts_end;
		rot_keys = oblistitem[i].rotkeys;
		rot_angle = 0;

		for (j = 1; j < rot_keys; j++)
		{
			frame_num = oblistitem[i].rot_track[j].framenum;

			if ((j < total_num_frames) && (frame_num < total_num_frames))
			{
				axis_x = oblistitem[i].rot_track[j].axis_x;
				axis_y = oblistitem[i].rot_track[j].axis_y;
				axis_z = oblistitem[i].rot_track[j].axis_z;

				rot_angle += oblistitem[i].rot_track[j].rotation_rad;

				rot_angle_x = rot_angle * axis_x;
				rot_angle_y = rot_angle * axis_y;
				rot_angle_z = rot_angle * axis_z;

				if (bEnable3dsLogfile)
				{
					fprintf(logfile, "local xyz: %f %f %f\n",
						oblistitem[i].local_centre_x,
						oblistitem[i].local_centre_y,
						oblistitem[i].local_centre_z);
				}

				for (v = v_start; v <= v_end; v++)
				{
					x = pmdata[pmodel_id].w[frame_num][v].x - oblistitem[i].local_centre_x;
					y = pmdata[pmodel_id].w[frame_num][v].y - oblistitem[i].local_centre_y;
					z = pmdata[pmodel_id].w[frame_num][v].z - oblistitem[i].local_centre_z;

					tx = x * (float)cos(rot_angle_z) + y * (float)sin(rot_angle_z);
					ty = y * (float)cos(rot_angle_z) - x * (float)sin(rot_angle_z);
					tz = z;

					x = tx;
					y = ty;
					z = tz;

					tx = x;
					ty = y * (float)cos(rot_angle_x) + z * (float)sin(rot_angle_x);
					tz = z * (float)cos(rot_angle_x) - y * (float)sin(rot_angle_x);

					x = tx;
					y = ty;
					z = tz;

					tx = x * (float)cos(rot_angle_y) - z * (float)sin(rot_angle_y);
					ty = y;
					tz = z * (float)cos(rot_angle_y) + x * (float)sin(rot_angle_y);

					if (bEnable3dsLogfile)
					{
						fprintf(logfile, "i: %d  frame: %d  v: %d  angle: %f  ", i, frame_num, v, rot_angle);
						fprintf(logfile, "pxyz: %f %f %f  xyz: %f %f %f\n", x, y, z,
							pmdata[pmodel_id].w[frame_num][v].x,
							pmdata[pmodel_id].w[frame_num][v].y,
							pmdata[pmodel_id].w[frame_num][v].z);
					}
					pmdata[pmodel_id].w[frame_num][v].x = tx + oblistitem[i].local_centre_x;
					pmdata[pmodel_id].w[frame_num][v].y = ty + oblistitem[i].local_centre_y;
					pmdata[pmodel_id].w[frame_num][v].z = tz + oblistitem[i].local_centre_z;
				}
				if (bEnable3dsLogfile)
					fprintf(logfile, "\n");
			}
		}
	}

	if (bEnable3dsLogfile)
		fprintf(logfile, "\n\nAPPLYING SCALE TRACK DATA\n\n");

	// Scale and rotate verts for all frames
	for (frame_num = 0; frame_num < total_num_frames; frame_num++)
	{
		for (i = 0; i < total_num_verts; i++)
		{
			x = scale * pmdata[pmodel_id].w[frame_num][i].x;
			y = scale * pmdata[pmodel_id].w[frame_num][i].y;
			z = scale * pmdata[pmodel_id].w[frame_num][i].z;

			angle = 0;

			tx = x * (float)cos(angle) + z * (float)sin(angle);
			ty = y;
			tz = z * (float)cos(angle) - x * (float)sin(angle);

			pmdata[pmodel_id].w[frame_num][i].x = tx;
			pmdata[pmodel_id].w[frame_num][i].y = ty;
			pmdata[pmodel_id].w[frame_num][i].z = tz;
		}
	}

	cnt = 0;

	for (i = 0; i < total_num_faces; i++)
	{
		for (j = 0; j < 3; j++)
		{
			pmdata[pmodel_id].f[cnt] = faces[i].v[j];

			pmdata[pmodel_id].t[cnt].x = mcoords[cnt].x;
			pmdata[pmodel_id].t[cnt].y = mcoords[cnt].y;

			cnt++;
		}
	}

	for (i = 0; i < total_num_objects; i++)
	{
		pmdata[pmodel_id].poly_cmd[i] = D3DPT_TRIANGLELIST;
		pmdata[pmodel_id].num_verts_per_object[i] = (int)num_verts_in_object[i];
		pmdata[pmodel_id].num_faces_per_object[i] = (int)num_faces_in_object[i];
		pmdata[pmodel_id].texture_list[i] = object_texture[i];
	}

	loading_first_model_flag = FALSE;

	if (bEnable3dsLogfile)
		fclose(logfile);

	pmdata[pmodel_id].num_polys_per_frame = total_num_objects;
	pmdata[pmodel_id].num_faces = total_num_faces;
	pmdata[pmodel_id].num_verts = total_num_verts;
	pmdata[pmodel_id].num_frames = total_num_frames;

	pmdata[pmodel_id].skx = 1;
	pmdata[pmodel_id].sky = 1;
	pmdata[pmodel_id].tex_alias = 7;
	pmdata[pmodel_id].use_indexed_primitive = TRUE;

	ReleaseTempMemory();

	return TRUE;
}

// PROCESS TRIANGLE DATA ROUTINES

BOOL ProcessVertexData(FILE* fp)
{
	int i, j;
	int temp_int;
	float p, temp_float;
	unsigned short num_vertices;

	last_num_verts = total_num_verts;
	oblistitem[total_num_objects].verts_start = total_num_verts;

	fread(&num_vertices, sizeof(num_vertices), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, "%s %d\n", "TRIANGLE_VERTEXLIST", num_vertices);

	num_verts_in_object[total_num_objects] = (short)num_vertices;

	if ((total_num_verts + num_vertices) >= MAX_NUM_3DS_VERTICES)
		return FALSE;

	for (i = 0; i < (int)num_vertices; i++)
	{
		for (j = 0; j < 3; j++)
		{
			fread(&p, sizeof(float), 1, fp);
			temp_int = (int)(((p * (float)10000) + (float)0.5));
			temp_float = (float)temp_int / (float)10000;
			fverts[total_num_verts][j] = temp_float;
		}

		if (bEnable3dsLogfile)
		{
			fprintf(logfile, " %f %f %f\n",
				fverts[total_num_verts][0],
				fverts[total_num_verts][1],
				fverts[total_num_verts][2]);
		}

		// set default mapping co-ordinates, in case none are defined in model
		mcoords[total_num_verts].x = 0;
		mcoords[total_num_verts].y = 0;

		total_num_verts++;
	}

	oblistitem[total_num_objects].verts_end = total_num_verts - 1;

	return TRUE;
}

BOOL ProcessFaceData(FILE* fp)
{
	int i, j, cnt = 0;
	unsigned short num_faces, face_index, ftemp;

	last_num_faces = total_num_faces;

	fread(&num_faces, sizeof(num_faces), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, "%s %d\n", "TRIANGLE_FACELIST ", num_faces);

	num_faces_in_object[total_num_objects] = (short)num_faces;

	if ((total_num_faces + num_faces) >= MAX_NUM_3DS_FACES)
		return FALSE;

	for (i = 0; i < (int)num_faces; i++)
	{
		for (j = 0; j < 4; j++)
		{
			fread(&face_index, sizeof(face_index), 1, fp);

			// note faces 1 to 3 are valid face indices, but the 4th one is NOT
			if (j < 3)
			{
				ftemp = (unsigned short)(face_index); // + last_num_verts);
				faces[total_num_faces].v[j] = ftemp;
				if (bEnable3dsLogfile)
					fprintf(logfile, " %d", face_index);
			}
			else
			{
				if (bEnable3dsLogfile)
				{
					fprintf(logfile, "   flags: %d => ", face_index);

					if ((face_index & 0x0001) == 0x0001)
						fprintf(logfile, "  AC: 1");
					else
						fprintf(logfile, "  AC: 0");

					if ((face_index & 0x0002) == 0x0002)
						fprintf(logfile, "  BC: 1");
					else
						fprintf(logfile, "  BC: 0");

					if ((face_index & 0x0004) == 0x0004)
						fprintf(logfile, "  AB: 1");
					else
						fprintf(logfile, "  AB: 0");
				}
			}
		}
		total_num_faces++;
		if (bEnable3dsLogfile)
			fprintf(logfile, "\n");
	}

	return TRUE;
}

void ProcessTriSmoothData(FILE* fp)
{
	int i, num_faces;
	BYTE a, b, c, d;

	num_faces = num_faces_in_object[total_num_objects];

	for (i = 0; i < num_faces; i++)
	{
		fread(&a, sizeof(BYTE), 1, fp);
		fread(&b, sizeof(BYTE), 1, fp);
		fread(&c, sizeof(BYTE), 1, fp);
		fread(&d, sizeof(BYTE), 1, fp);

		if (bEnable3dsLogfile)
			fprintf(logfile, " a,b,c,d : %d %d %d %d\n", a, b, c, d);
	}
}

void ProcessTriLocalData(FILE* fp)
{
	float x, y, z;
	float local_centre_x, local_centre_y, local_centre_z;

	fread(&x, sizeof(float), 1, fp);
	fread(&y, sizeof(float), 1, fp);
	fread(&z, sizeof(float), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, " x,y,z: %f %f %f\n", x, y, z);

	fread(&x, sizeof(float), 1, fp);
	fread(&y, sizeof(float), 1, fp);
	fread(&z, sizeof(float), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, " x,y,z: %f %f %f\n", x, y, z);

	fread(&x, sizeof(float), 1, fp);
	fread(&y, sizeof(float), 1, fp);
	fread(&z, sizeof(float), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, " x,y,z: %f %f %f\n", x, y, z);

	fread(&local_centre_x, sizeof(float), 1, fp);
	fread(&local_centre_y, sizeof(float), 1, fp);
	fread(&local_centre_z, sizeof(float), 1, fp);

	if (bEnable3dsLogfile)
	{
		fprintf(logfile, " local_centre_x,y,z : %f %f %f\n",
			local_centre_x, local_centre_y, local_centre_z);
	}

	oblistitem[total_num_objects].local_centre_x = local_centre_x;
	oblistitem[total_num_objects].local_centre_y = local_centre_y;
	oblistitem[total_num_objects].local_centre_z = local_centre_z;
}

// PROCESS TEXTURE, MATERIAL, AND MAPPING DATA ROUTINES

void AddMapName(FILE* fp, int pmodel_id)
{
	int i;
	BOOL error = TRUE;
	char map_name[256];

	// read in map name from file
	for (i = 0; i < 256; i++)
	{
		fread(&map_name[i], sizeof(char), 1, fp);
		if (map_name[i] == 0)
			break;
	}

	// remove file extention from string
	for (i = 0; i < 256; i++)
	{
		if (map_name[i] == '.')
		{
			map_name[i] = 0;
			break;
		}
	}

	// lookup texture alias
	for (i = 0; i < MAX_NUM_TEXTURES; i++)
	{
		if (_strcmpi(map_name, TexMap[i].tex_alias_name) == 0)
		{
			pmdata[pmodel_id].texture_maps[num_maps] = i;
			error = FALSE;
			break;
		}
	}

	if (error == TRUE)
	{
		//MessageBox(hwnd, "Error : AddMapName", map_name, MB_OK);
		strcpy_s(mapnames[num_maps], "error");
		return;
	}

	strcpy_s(mapnames[num_maps], map_name);
	if (bEnable3dsLogfile)
		fprintf(logfile, "%s %s\n", "MAPPING_NAME ", mapnames[num_maps]);
	num_maps++;
}

void AddMaterialName(FILE* fp)
{
	int i;
	BOOL error = TRUE;
	char mat_name[256];

	for (i = 0; i < 256; i++)
	{
		fread(&mat_name[i], sizeof(char), 1, fp);
		if (mat_name[i] == 0)
		{
			error = FALSE;
			break;
		}
	}

	if (error == TRUE)
	{
		//MessageBox(hwnd, "Error : AddMaterialName", NULL, MB_OK);
		strcpy_s(material_list[num_materials], "error");
		return;
	}

	if (bEnable3dsLogfile)
		fprintf(logfile, "MAT_NAME01  %s\n", mat_name);

	strcpy_s(material_list[num_materials], mat_name);
	num_materials++;
}

void ProcessMaterialData(FILE* fp, int pmodel_id)
{
	int i;
	short findex, current_texture;
	unsigned short num_faces;
	BOOL error = TRUE;
	char mat_name[256];

	for (i = 0; i < 256; i++)
	{
		fread(&mat_name[i], sizeof(char), 1, fp);
		if (mat_name[i] == 0)
			break;
	}

	for (i = 0; i < MAX_NUM_3DS_TEXTURES; i++)
	{
		if (_strcmpi(mat_name, material_list[i]) == 0)
		{
			current_texture = pmdata[pmodel_id].texture_maps[i];
			error = FALSE;
			break;
		}
	}

	if (error == TRUE)
	{
		//MessageBox(hwnd, "Error : ProcessMaterialData", NULL, MB_OK);
		strcpy_s(material_list[num_materials], "error");
		return;
	}

	fread(&num_faces, sizeof(num_faces), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, "TRIANGLE_MATERIAL %d\n", num_faces);

	for (i = 0; i < num_faces; i++)
	{
		fread(&findex, sizeof(short), 1, fp);
		faces[last_num_faces + findex].tex = current_texture;
		object_texture[total_num_objects] = current_texture;
	}
	return;
}

void ProcessMappingData(FILE* fp)
{
	int i;
	unsigned short num_mapping_coords;

	total_num_mcoords = last_num_verts;

	fread(&num_mapping_coords, sizeof(num_mapping_coords), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, "%s %d\n", "TRIANGLE_MAPPINGCOORS ", num_mapping_coords);

	for (i = 0; i < num_mapping_coords; i++)
	{
		fread(&mcoords[total_num_mcoords].x, sizeof(float), 1, fp);
		fread(&mcoords[total_num_mcoords].y, sizeof(float), 1, fp);

		if (bEnable3dsLogfile)
		{
			fprintf(logfile, " %f %f\n",
				mcoords[total_num_mcoords].x,
				mcoords[total_num_mcoords].y);
		}
		total_num_mcoords++;
	}
	return;
}

// KEYFRAME - PROCESS ANIMATION DATA ROUTINES

void ProcessPivots(FILE* fp)
{
	float x, y, z;

	fread(&x, sizeof(float), 1, fp);
	fread(&y, sizeof(float), 1, fp);
	fread(&z, sizeof(float), 1, fp);

	oblistitem[kf_count].pivot.x = x;
	oblistitem[kf_count].pivot.y = y;
	oblistitem[kf_count].pivot.z = z;

	if (bEnable3dsLogfile)
		fprintf(logfile, "PIVOT: %f %f %f\n", x, y, z);
}

void ProcessRotationTrack(FILE* fp)
{
	int i;
	short framenum;
	long lunknown;
	float rotation_rad;
	float axis_x;
	float axis_y;
	float axis_z;

	fread(&pt_flags, sizeof(short), 1, fp);

	fread(&unknown, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);

	fread(&oblistitem[kf_count].rotkeys, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, "ROT_TRACK_TAG %d\n", oblistitem[kf_count].rotkeys);

	for (i = 0; i < oblistitem[kf_count].rotkeys; i++)
	{
		fread(&framenum, sizeof(short), 1, fp);
		fread(&lunknown, sizeof(long), 1, fp);
		fread(&rotation_rad, sizeof(float), 1, fp);
		fread(&axis_x, sizeof(float), 1, fp);
		fread(&axis_y, sizeof(float), 1, fp);
		fread(&axis_z, sizeof(float), 1, fp);

		if (bEnable3dsLogfile)
		{
			fprintf(logfile, " framenum = %d  rot_angle/rads = %f  axis_x,y,z : %f %f %f\n",
				framenum, rotation_rad, axis_x, axis_y, axis_z);
		}

		if (i < total_num_frames)
		{
			oblistitem[kf_count].rot_track[i].framenum = framenum;
			oblistitem[kf_count].rot_track[i].lunknown = lunknown;
			oblistitem[kf_count].rot_track[i].rotation_rad = rotation_rad;
			oblistitem[kf_count].rot_track[i].axis_x = axis_x;
			oblistitem[kf_count].rot_track[i].axis_y = axis_y;
			oblistitem[kf_count].rot_track[i].axis_z = axis_z;
		}
	}
}

void ProcessPositionTrack(FILE* fp)
{

	int i;
	short framenum;
	long lunknown;
	float pos_x;
	float pos_y;
	float pos_z;

	fread(&pt_flags, sizeof(short), 1, fp);

	fread(&unknown, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);

	fread(&oblistitem[kf_count].poskeys, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, "POS_TRACK_TAG %d\n", oblistitem[kf_count].poskeys);

	for (i = 0; i < oblistitem[kf_count].poskeys; i++)
	{
		fread(&framenum, sizeof(short), 1, fp);
		fread(&lunknown, sizeof(long), 1, fp);
		fread(&pos_x, sizeof(float), 1, fp);
		fread(&pos_y, sizeof(float), 1, fp);
		fread(&pos_z, sizeof(float), 1, fp);

		if (bEnable3dsLogfile)
		{
			fprintf(logfile, " framenum = %d   pos_x,y,z : %f %f %f\n",
				framenum, pos_x, pos_y, pos_z);
		}

		if (i < total_num_frames)
		{
			oblistitem[kf_count].pos_track[i].framenum = framenum;
			oblistitem[kf_count].pos_track[i].lunknown = lunknown;
			oblistitem[kf_count].pos_track[i].pos_x = pos_x;
			oblistitem[kf_count].pos_track[i].pos_y = pos_y;
			oblistitem[kf_count].pos_track[i].pos_z = pos_z;
		}
	}
}

void ProcessScaleTrack(FILE* fp)
{
	int i;
	short framenum;
	long lunknown;
	float scale_x;
	float scale_y;
	float scale_z;

	fread(&pt_flags, sizeof(short), 1, fp);

	fread(&unknown, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);

	fread(&oblistitem[kf_count].sclkeys, sizeof(short), 1, fp);
	fread(&unknown, sizeof(short), 1, fp);

	if (bEnable3dsLogfile)
		fprintf(logfile, "SCL_TRACK_TAG %d\n", oblistitem[kf_count].sclkeys);

	for (i = 0; i < oblistitem[kf_count].sclkeys; i++)
	{
		fread(&framenum, sizeof(short), 1, fp);
		fread(&lunknown, sizeof(long), 1, fp);

		fread(&scale_x, sizeof(float), 1, fp);
		fread(&scale_y, sizeof(float), 1, fp);
		fread(&scale_z, sizeof(float), 1, fp);

		if (bEnable3dsLogfile)
		{
			fprintf(logfile, " framenum = %d   x,y,z : %f %f %f\n",
				framenum, scale_x, scale_y, scale_z);
		}
	}
}

void ProcessNodeId(FILE* fp)
{
	short node_id;

	fread(&node_id, sizeof(short), 1, fp);
	if (bEnable3dsLogfile)
		fprintf(logfile, "NODE_ID  %d\n", node_id);
}

void ProcessNodeHeader(FILE* fp)
{
	int i;
	short flags1, flags2, heirarchy;
	char node_name[256];

	// read in node name from file

	for (i = 0; i < 256; i++)
	{
		fread(&node_name[i], sizeof(char), 1, fp);
		if (node_name[i] == 0)
			break;
	}

	fread(&flags1, sizeof(short), 1, fp);
	fread(&flags2, sizeof(short), 1, fp);
	fread(&heirarchy, sizeof(short), 1, fp);

	if (bEnable3dsLogfile)
	{
		fprintf(logfile, "NODE_HDR %s\n", node_name);
		fprintf(logfile, " flags1 %d\n", flags1);
		fprintf(logfile, " flags2 %d\n", flags2);
		fprintf(logfile, " heirarchy %d\n", heirarchy);
	}
}

void Process3DSVersion(FILE* fp)
{
	short version;

	fread(&version, sizeof(short), 1, fp);
	if (bEnable3dsLogfile)
		fprintf(logfile, "3DS VERSION %d\n", version);
	fread(&version, sizeof(short), 1, fp);
}

void ProcessMasterScale(FILE* fp)
{
	float master_scale;

	fread(&master_scale, sizeof(float), 1, fp);
	if (bEnable3dsLogfile)
		fprintf(logfile, "MASTER_SCALE %f\n", master_scale);
}

// RELEASE AND DEBUG ROUTINES

void ReleaseTempMemory()
{
}

void PrintLogFile(FILE* logfile, char* commmand)
{

	/*
	if(bEnable3dsLogfile)
	{
		fprintf(logfile, commmand);
		fprintf(logfile,"\n");
	}
	*/
	command_found_flag = TRUE;
}
