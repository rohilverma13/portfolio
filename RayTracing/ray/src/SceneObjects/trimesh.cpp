#include "trimesh.h"
#include <assert.h>
#include <float.h>
#include <string.h>
#include <algorithm>
#include <cmath>
#include "../ui/TraceUI.h"
extern TraceUI* traceUI;

using namespace std;

Trimesh::~Trimesh()
{
	for (auto m : materials)
		delete m;
	for (auto f : faces)
		delete f;
}
//test
// must add vertices, normals, and materials IN ORDER
void Trimesh::addVertex(const glm::dvec3& v)
{
	vertices.emplace_back(v);
}

void Trimesh::addMaterial(Material* m)
{
	materials.emplace_back(m);
}

void Trimesh::addNormal(const glm::dvec3& n)
{
	normals.emplace_back(n);
}

// Returns false if the vertices a,b,c don't all exist
bool Trimesh::addFace(int a, int b, int c)
{
	int vcnt = vertices.size();

	if (a >= vcnt || b >= vcnt || c >= vcnt)
		return false;

	TrimeshFace* newFace = new TrimeshFace(
	        scene, new Material(*this->material), this, a, b, c);
	newFace->setTransform(this->transform);
	if (!newFace->degen)
		faces.push_back(newFace);
	else
		delete newFace;

	// Don't add faces to the scene's object list so we can cull by bounding
	// box
	return true;
}

// Check to make sure that if we have per-vertex materials or normals
// they are the right number.
const char* Trimesh::doubleCheck()
{
	if (!materials.empty() && materials.size() != vertices.size())
		return "Bad Trimesh: Wrong number of materials.";
	if (!normals.empty() && normals.size() != vertices.size())
		return "Bad Trimesh: Wrong number of normals.";

	return 0;
}

bool Trimesh::intersectLocal(ray& r, isect& i) const
{
	bool have_one = false;
	for (auto face : faces) {
		isect cur;
		if (face->intersectLocal(r, cur)) {
			if (!have_one || (cur.getT() < i.getT())) {
				i = cur;
				have_one = true;
			}
		}
	}
	if (!have_one)
		i.setT(1000.0);
	return have_one;
}

bool TrimeshFace::intersect(ray& r, isect& i) const
{
	return intersectLocal(r, i);
}

// Intersect ray r with the triangle abc.  If it hits returns true,
// and put the parameter in t and the barycentric coordinates of the
// intersection in u (alpha) and v (beta).
bool TrimeshFace::intersectLocal(ray& r, isect& i) const
{

	// FIXME: Add ray-trimesh intersection
	double tnum = (glm::dot(normal, r.getPosition()) + (-1.0 * dist));
	double tdenom = (glm::dot(normal, (r.getDirection() * 1.0)));
	double t = (-1.0 * (tnum / tdenom));
	if (tdenom == 0 || t < 0){
		return false;
	}
	glm::dvec3 Q = r.getPosition() + (r.getDirection() * t);
	//Q = r.at(t);
	glm::dvec3 a_coord = (parent->vertices[ids[0]]);
	glm::dvec3 b_coord = (parent->vertices[ids[1]]);
	glm::dvec3 c_coord = (parent->vertices[ids[2]]);
	glm::dvec3 vaq = (Q - a_coord);
	glm::dvec3 vbq = (Q - b_coord);
	glm::dvec3 vcq = (Q - c_coord);
	glm::dvec3 vca = (a_coord - c_coord);	
	glm::dvec3 vab = (b_coord - a_coord);
	glm::dvec3 vac = (c_coord - a_coord);
	glm::dvec3 vbc = (c_coord - b_coord);
	glm::dvec3 vqa = (a_coord - Q);
	glm::dvec3 vqb = (b_coord - Q);
	glm::dvec3 vqc = (c_coord - Q);

	//do inside/outside tests to see if Q is in tri	
	double sideOfAB = (glm::dot(normal, glm::cross(vab, vaq))); 
	double sideOfBC = (glm::dot(normal, glm::cross(vbc, vbq)));
	double sideOfCA = (glm::dot(normal, glm::cross(vca, vcq)));
	
	if ((sideOfAB >= 0) && (sideOfBC >= 0) && (sideOfCA >= 0)){
		i.setObject(this);
		i.setT(t);
		i.setMaterial(this->getMaterial());
		i.setN(normal);
		//get areas
		double abc = (glm::dot(glm::cross(vab, vac), normal));
		double qbc = (glm::dot(glm::cross(vqb, vqc), normal));
		double qca = (glm::dot(glm::cross(vqc, vqa), normal));
		double alpha = (qbc / abc);
		double beta = (qca / abc);
		double gamma = 1.0 - alpha - beta;
		i.setBary(alpha, beta, gamma);
		i.setUVCoordinates(glm::dvec2(alpha, beta));
		
		glm::dvec3 norm;
		//phong interpolation
		if (parent->vertNorms){ //normals are per vertex
			glm::dvec3 avec = (alpha * (parent->normals[ids[0]]));
			glm::dvec3 bvec = (beta * (parent->normals[ids[1]]));
			glm::dvec3 cvec = (gamma * (parent->normals[ids[2]]));
			avec = glm::normalize(avec);
			bvec = glm::normalize(bvec);
			cvec = glm::normalize(cvec);
			norm = (avec + bvec + cvec);
		} else {
			norm = glm::dvec3(normal[0] * alpha, normal[1] * beta,
				normal[2] * gamma);
			norm = glm::normalize(norm);
		} 
		double checkNorm = glm::dot(norm, r.getDirection());
		double backOfTri = (checkNorm < 0.0) - (checkNorm > 0.0);
		norm = backOfTri * norm;
		i.setN(norm);

		//loop through materials & interpolate vals for each one
		if (parent->materials.size() > 0) {
			Material sumMat = Material();
			sumMat = (alpha * *parent->materials[ids[0]]);
			sumMat += (beta * *parent->materials[ids[1]]);
			sumMat += (gamma * *parent->materials[ids[2]]);
			i.setMaterial(sumMat);
		} else {
			i.setMaterial(this->getMaterial());
		}
		return true;
	}
	return false;
}


// Once all the verts and faces are loaded, per vertex normals can be
// generated by averaging the normals of the neighboring faces.
void Trimesh::generateNormals()
{
	int cnt = vertices.size();
	normals.resize(cnt);
	std::vector<int> numFaces(cnt, 0);

	for (auto face : faces) {
		glm::dvec3 faceNormal = face->getNormal();

		for (int i = 0; i < 3; ++i) {
			normals[(*face)[i]] += faceNormal;
			++numFaces[(*face)[i]];
		}
	}

	for (int i = 0; i < cnt; ++i) {
		if (numFaces[i])
			normals[i] /= numFaces[i];
	}

	vertNorms = true;
}
