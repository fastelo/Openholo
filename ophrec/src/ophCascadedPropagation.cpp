/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install, copy or use the software.
//
//
//                           License Agreement
//                For Open Source Digital Holographic Library
//
// Openholo library is free software;
// you can redistribute it and/or modify it under the terms of the BSD 2-Clause license.
//
// Copyright (C) 2017-2024, Korea Electronics Technology Institute. All rights reserved.
// E-mail : contact.openholo@gmail.com
// Web : http://www.openholo.org
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//  1. Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//  2. Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the copyright holder or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
// This software contains opensource software released under GNU Generic Public License,
// NVDIA Software License Agreement, or CUDA supplement to Software License Agreement.
// Check whether software you use contains licensed software.
//
//M*/

#include "ophCascadedPropagation.h"
#include "sys.h"
#include "tinyxml2.h"
#include <string>
#include <cfloat>



ophCascadedPropagation::ophCascadedPropagation()
	: ready_to_propagate(false),
	hologram_path(L"")
{
}

ophCascadedPropagation::ophCascadedPropagation(const wchar_t* configfilepath)
	: ready_to_propagate(false),
	hologram_path(L"")
{
	if (readConfig(configfilepath) && allocateMem())
	{
		string hologram_path_str;
		hologram_path_str.assign(hologram_path.begin(), hologram_path.end());
		ready_to_propagate = (sourcetype_ == SourceType::IMG && loadInputImg(hologram_path_str)) || (sourcetype_ == SourceType::OHC && loadAsOhc(hologram_path_str.c_str()));
	}
}

ophCascadedPropagation::~ophCascadedPropagation()
{
}

void ophCascadedPropagation::ophFree()
{
	deallocateMem();
}

bool ophCascadedPropagation::propagate()
{
	if (!isReadyToPropagate())
	{
		PRINT_ERROR("module not initialized");
		return false;
	}

	if (!propagateSlmToPupil())
	{
		PRINT_ERROR("failed to propagate to pupil plane");
		return false;
	}

	if (!propagatePupilToRetina())
	{
		PRINT_ERROR("failed to propagate to retina plane");
		return false;
	}

	return true;
}

bool ophCascadedPropagation::save(const wchar_t* pathname, uint8_t bitsperpixel)
{
	wstring bufw(pathname);
	string bufs;
	bufs.assign(bufw.begin(), bufw.end());
	oph::uchar* src = getIntensityfields(getRetinaWavefieldAll());
	return saveAsImg(bufs.c_str(), bitsperpixel, src, getResX(), getResY());
}

bool ophCascadedPropagation::saveAsOhc(const char * fname)
{
	oph::uint nx = getResX();
	oph::uint ny = getResY();
	for (oph::uint i = 0; i < getNumColors(); i++)
		memcpy(complex_H[i], wavefield_retina[i], nx * ny * sizeof(Complex<Real>));

	if (!context_.wave_length)
		context_.wave_length = new Real[getNumColors()];
	for (oph::uint i = 0; i < getNumColors(); i++)
		context_.wave_length[i] = config_.wavelengths[i];
	context_.pixel_pitch[0] = config_.dx;
	context_.pixel_pitch[1] = config_.dy;
	context_.pixel_number[0] = config_.nx;
	context_.pixel_number[1] = config_.ny;

	return Openholo::saveAsOhc(fname);
}

bool ophCascadedPropagation::loadAsOhc(const char * fname)
{
	if (!Openholo::loadAsOhc(fname))
		return -1;

	oph::uint nx = getResX();
	oph::uint ny = getResY();
	config_.num_colors = OHC_decoder->getNumOfWavlen();
	for (oph::uint i = 0; i < getNumColors(); i++)
		memcpy(wavefield_SLM[i], complex_H[i], nx * ny * sizeof(Complex<Real>));

	return 1;
}

bool ophCascadedPropagation::allocateMem()
{
	wavefield_SLM.resize(getNumColors());
	wavefield_pupil.resize(getNumColors());
	wavefield_retina.resize(getNumColors());
	oph::uint nx = getResX();
	oph::uint ny = getResY();
	complex_H = new Complex<Real>*[getNumColors()];
	for (oph::uint i = 0; i < getNumColors(); i++)
	{
		wavefield_SLM[i] = new oph::Complex<Real>[nx * ny];
		wavefield_pupil[i] = new oph::Complex<Real>[nx * ny];
		wavefield_retina[i] = new oph::Complex<Real>[nx * ny];
		complex_H[i] = new oph::Complex<Real>[nx * ny];
	}
	
	return true;
}

void ophCascadedPropagation::deallocateMem()
{
	for (auto e : wavefield_SLM)
		delete[] e;
	wavefield_SLM.clear();

	for (auto e : wavefield_pupil)
		delete[] e;
	wavefield_pupil.clear();
	
	for (auto e : wavefield_retina)
		delete[] e;
	wavefield_retina.clear();

	for (oph::uint i = 0; i < getNumColors(); i++)
		delete[] complex_H[i];
}

// read in hologram data
bool ophCascadedPropagation::loadInputImg(string hologram_path_str)
{
	if (!checkExtension(hologram_path_str.c_str(), ".bmp"))
	{
		PRINT_ERROR("input file format not supported");
		return false;
	}
	oph::uint nx = getResX();
	oph::uint ny = getResY();
	oph::uchar* data = new oph::uchar[nx * ny * getNumColors()];
	if (!loadAsImgUpSideDown(hologram_path_str.c_str(), data))	// loadAsImg() keeps to fail
	{
		PRINT_ERROR("input file not found");
		delete[] data;
		return false;
	}

	// copy data to wavefield
	try {
		oph::uint numColors = getNumColors();
		for (oph::uint row = 0; row < ny; row++)
		{
			for (oph::uint col = 0; col < nx; col++)
			{
				for (oph::uint color = 0; color < numColors; color++)
				{
					// BGR to RGB & upside-down
					wavefield_SLM[numColors - 1 - color][(ny - 1 - row) * nx+ col] = oph::Complex<Real>((Real)data[(row * nx + col) * numColors + color], 0);
				}
			}
		}
	}

	catch (...) {
		PRINT_ERROR("failed to generate wavefield from bmp");
		delete[] data;
		return false;
	}

	delete[] data;
	return true;
}

oph::uchar* ophCascadedPropagation::getIntensityfields(vector<oph::Complex<Real>*> waveFields)
{
	oph::uint numColors = getNumColors();
	if (numColors != 1 && numColors != 3)
	{
		PRINT_ERROR("invalid number of color channels");
		return nullptr;
	}

	oph::uint nx = getResX();
	oph::uint ny = getResY();
	oph::uchar* intensityField = new oph::uchar[nx * ny * numColors];
	for (oph::uint color = 0; color < numColors; color++)
	{
		Real* intensityFieldUnnormalized = new Real[nx * ny];

		// find minmax
		Real maxIntensity = 0.0;
		Real minIntensity = REAL_IS_DOUBLE ? DBL_MAX : FLT_MAX;
		for (oph::uint row = 0; row < ny; row++)
		{
			for (oph::uint col = 0; col < nx; col++)
			{
				intensityFieldUnnormalized[row * nx + col] = waveFields[color][row * nx + col].mag2();
				maxIntensity = max(maxIntensity, intensityFieldUnnormalized[row * nx + col]);
				minIntensity = min(minIntensity, intensityFieldUnnormalized[row * nx + col]);
			}
		}

		maxIntensity *= getNor();			// IS IT REALLY NEEDED?
		if (maxIntensity <= minIntensity)
		{
			for (oph::uint row = 0; row < ny; row++)
			{
				for (oph::uint col = 0; col < nx; col++)
				{
					intensityField[(row * nx + col) * numColors + (numColors - 1 - color)] = 0;	// flip RGB order
				}
			}
		}
		else
		{
			for (oph::uint row = 0; row < ny; row++)
			{
				for (oph::uint col = 0; col < nx; col++)
				{
					Real normalizedVal = (intensityFieldUnnormalized[row * nx + col] - minIntensity) / (maxIntensity - minIntensity);
					normalizedVal = min(1.0, normalizedVal);

					// rotate 180 & RGB flip
					intensityField[((ny - 1 - row) * nx + (nx - 1 - col)) * numColors + (numColors - 1 - color)] = (oph::uchar)(normalizedVal * 255);
				}
			}
		}
		delete[] intensityFieldUnnormalized;
	}

	return intensityField;
}

bool ophCascadedPropagation::readConfig(const wchar_t* fname)
{
	/*XML parsing*/
	tinyxml2::XMLDocument xml_doc;
	tinyxml2::XMLNode *xml_node;
	wstring fnamew(fname);
	string fnames;
	fnames.assign(fnamew.begin(), fnamew.end());

	if (!checkExtension(fnames.c_str(), ".xml"))
	{
		LOG("file's extension is not 'xml'\n");
		return false;
	}
	auto ret = xml_doc.LoadFile(fnames.c_str());
	if (ret != tinyxml2::XML_SUCCESS)
	{
		LOG("Failed to load file \"%s\"\n", fnames.c_str());
		return false;
	}

	xml_node = xml_doc.FirstChild();
	auto next = xml_node->FirstChildElement("SourceType");
	if (!next || !(next->GetText()))
		return false;
	string sourceTypeStr = (xml_node->FirstChildElement("SourceType"))->GetText();
	if (sourceTypeStr == string("IMG"))
		sourcetype_ = SourceType::IMG;
	else if (sourceTypeStr == string("OHC"))
		sourcetype_ = SourceType::OHC;

	next = xml_node->FirstChildElement("NumColors");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryUnsignedText(&config_.num_colors))
		return false;
	if (getNumColors() == 0 || getNumColors() > 3)
		return false;

	if (config_.num_colors >= 1)
	{
		next = xml_node->FirstChildElement("WavelengthR");
		if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.wavelengths[0]))
			return false;
	}
	if (config_.num_colors >= 2)
	{
		next = xml_node->FirstChildElement("WavelengthG");
		if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.wavelengths[1]))
			return false;
	}
	if (config_.num_colors == 3)
	{
		next = xml_node->FirstChildElement("WavelengthB");
		if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.wavelengths[2]))
			return false;
	}

	next = xml_node->FirstChildElement("PixelPitchHor");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.dx))
		return false;
	next = xml_node->FirstChildElement("PixelPitchVer");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.dy))
		return false;
	if (getPixelPitchX() != getPixelPitchY())
	{
		PRINT_ERROR("current implementation assumes pixel pitches are same for X and Y axes");
		return false;
	}

	next = xml_node->FirstChildElement("ResolutionHor");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryUnsignedText(&config_.nx))
		return false;
	next = xml_node->FirstChildElement("ResolutionVer");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryUnsignedText(&config_.ny))
		return false;

	if (!context_.wave_length)
		context_.wave_length = new Real[getNumColors()];
	for (oph::uint i = 0; i < getNumColors(); i++)
		context_.wave_length[i] = config_.wavelengths[i];
	context_.pixel_pitch[0] = config_.dx;
	context_.pixel_pitch[1] = config_.dy;
	context_.pixel_number[0] = config_.nx;
	context_.pixel_number[1] = config_.ny;

	setPixelNumberOHC(context_.pixel_number);
	setPixelPitchOHC(context_.pixel_pitch);
	for (oph::uint i = 0; i < getNumColors(); i++)
		addWaveLengthOHC(context_.wave_length[i]);

	// 2024.04.23. mwnam
// set variable for resolution
	resCfg = context_.pixel_number;

	next = xml_node->FirstChildElement("FieldLensFocalLength");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.field_lens_focal_length))
		return false;
	next = xml_node->FirstChildElement("DistReconstructionPlaneToPupil");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.dist_reconstruction_plane_to_pupil))
		return false;
	next = xml_node->FirstChildElement("DistPupilToRetina");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.dist_pupil_to_retina))
		return false;
	next = xml_node->FirstChildElement("PupilDiameter");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.pupil_diameter))
		return false;
	next = xml_node->FirstChildElement("Nor");
	if (!next || tinyxml2::XML_SUCCESS != next->QueryDoubleText(&config_.nor))
		return false;

	next = xml_node->FirstChildElement("HologramPath");
	if (!next || !(next->GetText()))
		return false;
	string holopaths = (xml_node->FirstChildElement("HologramPath"))->GetText();
	hologram_path.assign(holopaths.begin(), holopaths.end());

	return true;
}

bool ophCascadedPropagation::propagateSlmToPupil()
{
	auto start_time = CUR_TIME;
	oph::uint numColors = getNumColors();
	oph::uint nx = getResX();
	oph::uint ny = getResY();
	oph::Complex<Real>* buf = new oph::Complex<Real>[nx * ny];
	for (oph::uint color = 0; color < numColors; color++)
	{
		fft2(getSlmWavefield(color), buf, nx, ny, OPH_FORWARD, false);

		Real k = 2 * M_PI / getWavelengths()[color];
		Real vw = getWavelengths()[color] * getFieldLensFocalLength() / getPixelPitchX();
		Real dx1 = vw / (Real)nx;
		Real dy1 = vw / (Real)ny;
		for (oph::uint row = 0; row < ny; row++)
		{
			Real Y1 = ((Real)row - ((Real)ny - 1) * 0.5f) * dy1;
			for (oph::uint col = 0; col < nx; col++)
			{
				Real X1 = ((Real)col - ((Real)nx - 1) * 0.5f) * dx1;

				// 1st propagation
				oph::Complex<Real> t1 = oph::Complex<Real>(0, k / 2 / getFieldLensFocalLength() * (X1 * X1 + Y1 * Y1)).exp();
				oph::Complex<Real> t2(0, getWavelengths()[color] * getFieldLensFocalLength());
				buf[row * nx + col] = t1 / t2 * buf[row * nx + col];

				// applying aperture: need some optimization later
				if ((sqrt(X1 * X1 + Y1 * Y1) >= getPupilDiameter() / 2) || (row >= ny / 2 - 1))
					buf[row * nx + col] = 0;

				Real f_eye = (getFieldLensFocalLength() - getDistObjectToPupil()) * getDistPupilToRetina() / (getFieldLensFocalLength() - getDistObjectToPupil() + getDistPupilToRetina());
				oph::Complex<Real> t3 = oph::Complex<Real>(0, -k / 2 / f_eye * (X1 * X1 + Y1 * Y1)).exp();
				buf[row * nx + col] *= t3;
			}
		}

		memcpy(getPupilWavefield(color), buf, sizeof(oph::Complex<Real>) * nx * ny);
	}

	auto end_time = CUR_TIME;

	auto during_time = ((std::chrono::duration<Real>)(end_time - start_time)).count();

	LOG("SLM to Pupil propagation - Implement time : %.5lf sec\n", during_time);

	delete[] buf;
	return true;
}

bool ophCascadedPropagation::propagatePupilToRetina()
{
	auto start_time = CUR_TIME;
	oph::uint numColors = getNumColors();
	oph::uint nx = getResX();
	oph::uint ny = getResY();
	oph::Complex<Real>* buf = new oph::Complex<Real>[nx * ny];
	for (oph::uint color = 0; color < numColors; color++)
	{
		memcpy(buf, getPupilWavefield(color), sizeof(oph::Complex<Real>) * nx * ny);

		Real k = 2 * M_PI / getWavelengths()[color];
		Real vw = getWavelengths()[color] * getFieldLensFocalLength() / getPixelPitchX();
		Real dx1 = vw / (Real)nx;
		Real dy1 = vw / (Real)ny;
		for (oph::uint row = 0; row < ny; row++)
		{
			Real Y1 = ((Real)row - ((Real)ny - 1) * 0.5f) * dy1;
			for (oph::uint col = 0; col < nx; col++)
			{
				Real X1 = ((Real)col - ((Real)nx - 1) * 0.5f) * dx1;

				// 2nd propagation
				oph::Complex<Real> t1 = oph::Complex<Real>(0, k / 2 / getDistPupilToRetina() * (X1 * X1 + Y1 * Y1)).exp();
				buf[row * nx + col] *= t1;
			}
		}

		fft2(buf, getRetinaWavefield(color), nx, ny, OPH_FORWARD, false);
	}

	auto end_time = CUR_TIME;

	auto during_time = ((std::chrono::duration<Real>)(end_time - start_time)).count();

	LOG("Pupil to Retina propagation - Implement time : %.5lf sec\n", during_time);

	delete[] buf;
	return true;
}

oph::Complex<Real>* ophCascadedPropagation::getSlmWavefield(oph::uint id)
{
	if (wavefield_SLM.size() <= (size_t)id)
		return nullptr;
	return wavefield_SLM[id];
}

oph::Complex<Real>* ophCascadedPropagation::getPupilWavefield(oph::uint id)
{
	if (wavefield_pupil.size() <= (size_t)id)
		return nullptr;
	return wavefield_pupil[id];
}

oph::Complex<Real>* ophCascadedPropagation::getRetinaWavefield(oph::uint id)
{
	if (wavefield_retina.size() <= (size_t)id)
		return nullptr;
	return wavefield_retina[id];
}

vector<oph::Complex<Real>*> ophCascadedPropagation::getRetinaWavefieldAll()
{
	return wavefield_retina;
}


