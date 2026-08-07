//
// Copyright(c) 2016-2017 benikabocha.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)
//

#ifndef SABA_MODEL_MMDMODEL_MMDPHYSICS_H_
#define SABA_MODEL_MMDMODEL_MMDPHYSICS_H_

#include "PMDFile.h"
#include "PMXFile.h"

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include <vector>
#include <memory>
#include <cinttypes>

// Bullet Types
class btRigidBody;
class btCollisionShape;
class btTypedConstraint;
class btDiscreteDynamicsWorld;
class btBroadphaseInterface;
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btSequentialImpulseConstraintSolver;
class btMotionState;
class btTransform;
struct btOverlapFilterCallback;

namespace saba
{
	class MMDPhysics;
	class MMDModel;
	class MMDNode;

	class MMDMotionState;

	// R1.3 linked-body collision A/B (default keeps the Saba baseline:
	// PMX mask only, no Bullet linked-body disable).
	enum class MMDLinkedBodyCollisionMode
	{
		PmxMaskOnly,
		DisableConstraintLinkedPairs,
	};

	class MMDRigidBody
	{
	public:
		MMDRigidBody();
		~MMDRigidBody();
		MMDRigidBody(const MMDRigidBody& rhs) = delete;
		MMDRigidBody& operator = (const MMDRigidBody& rhs) = delete;

		bool Create(const PMDRigidBodyExt& pmdRigidBody, MMDModel* model, MMDNode* node);
		bool Create(const PMXRigidbody& pmxRigidBody, MMDModel* model, MMDNode* node);
		void Destroy();

		btRigidBody* GetRigidBody() const;
		uint16_t GetGroup() const;
		uint16_t GetGroupMask() const;
		// R1.2B deterministic-restore narrow interface.
		int GetRigidBodyType() const;             // 0=Kinematic 1=Dynamic 2=Aligned
		int32_t GetBoneIndex() const;
		const glm::mat4& GetOffsetMatrix() const;
		float GetDefinitionMass() const;          // PMX raw mass (bit pattern)
		void SelectMotionStateForMode(int mode);  // mode 0 = FollowBone
		void NormalizeCanonicalActivation(int mode);
		// Copies the rigid body's current COM transform into the active
		// motion state so reflect/write-back phases read the restored pose.
		void SyncActiveMotionStateToBodyTransform();
		// Sets the active motion state to an explicit transform. Used by the
		// deterministic restore path to reproduce the interpolated transform
		// that Bullet's synchronizeSingleMotionState writes at a canonical
		// frame boundary (see R1.2B Phase 1).
		void SyncActiveMotionStateToTransform(const btTransform& transform);

		void SetActivation(bool activation);
		void ResetTransform();
		void Reset(MMDPhysics* physics);
		// R1.3 Mode 2 A/B: false makes the merged motion state write the
		// full physics transform (diagnostic); true preserves the animated
		// translation (Saba baseline).
		void SetMode2PreserveTranslation(bool preserve);
		bool GetMode2PreserveTranslation() const;

		void ReflectGlobalTransform();
		void CalcLocalTransform();

		glm::mat4 GetTransform();

	private:
		enum class RigidBodyType
		{
			Kinematic,
			Dynamic,
			Aligned,
		};

	private:
		std::unique_ptr<btCollisionShape>	m_shape;
		std::unique_ptr<MMDMotionState>		m_activeMotionState;
		std::unique_ptr<MMDMotionState>		m_kinematicMotionState;
		std::unique_ptr<btRigidBody>		m_rigidBody;

		RigidBodyType	m_rigidBodyType;
		uint16_t		m_group;
		uint16_t		m_groupMask;

		MMDNode*	m_node;
		glm::mat4	m_offsetMat;
		int32_t		m_boneIndex;
		float		m_definitionMass;
		bool		m_mode2PreserveTranslation = true;

		std::string					m_name;
	};

	class MMDJoint
	{
	public:
		MMDJoint();
		~MMDJoint();
		MMDJoint(const MMDJoint& rhs) = delete;
		MMDJoint& operator = (const MMDJoint& rhs) = delete;

		bool CreateJoint(const PMDJointExt& pmdJoint, MMDRigidBody* rigidBodyA, MMDRigidBody* rigidBodyB);
		bool CreateJoint(const PMXJoint& pmxJoint, MMDRigidBody* rigidBodyA, MMDRigidBody* rigidBodyB);
		void Destroy();

		btTypedConstraint* GetConstraint() const;
		// R1.2B: clears cached joint impulses so restored worlds do not
		// inherit warm-start history.
		void ResetConstraintImpulses();

	private:
		std::unique_ptr<btTypedConstraint>	m_constraint;
	};

	class MMDPhysics
	{
	public:
		MMDPhysics();
		~MMDPhysics();

		MMDPhysics(const MMDPhysics& rhs) = delete;
		MMDPhysics& operator = (const MMDPhysics& rhs) = delete;

		bool Create();
		void Destroy();

		void SetFPS(float fps);
		float GetFPS() const;
		void SetMaxSubStepCount(int numSteps);
		int GetMaxSubStepCount() const;
		// Steps the dynamics world. Returns the number of fixed substeps
		// actually executed (the raw stepSimulation return value), so
		// WISTERIA can verify exact 30Hz->120Hz replay without guessing from
		// final state. Existing callers may ignore the return value.
		int Update(float time);
		// R1.2B deterministic restore: rebuild the collision world from the
		// current transforms (AABBs, pairs, manifolds) and clear solver
		// warm-start history. Semantics are defined by the R1.2B contract.
		void ClearContactManifoldsDeterministic();
		void RebuildCollisionWorldDeterministic();
		void ClearSolverHistoryDeterministic();
		// Deterministic replay narrow interface: read/clear Bullet's internal
		// frame accumulator without stepping the world. Used to guarantee a
		// Canonical Frame Boundary (remaining accumulator == 0) after
		// canonical resets. Not a Bullet parameter-control surface.
		float GetSimulationTime() const;
		void ResetSimulationTime();

		void AddRigidBody(MMDRigidBody* mmdRB);
		void RemoveRigidBody(MMDRigidBody* mmdRB);
		void AddJoint(MMDJoint* mmdJoint);
		void RemoveJoint(MMDJoint* mmdJoint);
		// R1.3 linked-body collision A/B.
		void SetLinkedBodyCollisionMode(MMDLinkedBodyCollisionMode mode);
		MMDLinkedBodyCollisionMode GetLinkedBodyCollisionMode() const;
		// Re-applies the policy to every world constraint (remove + add with
		// the Bullet disable flag). No-op before Create().
		void ApplyLinkedBodyCollisionMode();

		btDiscreteDynamicsWorld* GetDynamicsWorld() const;

	private:
		std::unique_ptr<btBroadphaseInterface>				m_broadphase;
		std::unique_ptr<btDefaultCollisionConfiguration>	m_collisionConfig;
		std::unique_ptr<btCollisionDispatcher>				m_dispatcher;
		std::unique_ptr<btSequentialImpulseConstraintSolver>	m_solver;
		std::unique_ptr<btDiscreteDynamicsWorld>			m_world;
		std::unique_ptr<btCollisionShape>					m_groundShape;
		std::unique_ptr<btMotionState>						m_groundMS;
		std::unique_ptr<btRigidBody>						m_groundRB;
		std::unique_ptr<btOverlapFilterCallback>			m_filterCB;

		double	m_fps;
		int		m_maxSubStepCount;
		MMDLinkedBodyCollisionMode	m_linkedBodyCollisionMode =
			MMDLinkedBodyCollisionMode::PmxMaskOnly;
	};

}
#endif // SABA_MODEL_MMDMODEL_MMDPHYSICS_H_

