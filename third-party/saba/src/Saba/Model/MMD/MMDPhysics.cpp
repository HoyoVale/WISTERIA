//
// Copyright(c) 2016-2017 benikabocha.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)
//

#include "MMDPhysics.h"

#include "MMDNode.h"
#include "MMDModel.h"
#include "Saba/Base/Log.h"

#include <glm/gtc/matrix_transform.hpp>

#include <btBulletCollisionCommon.h>
#include <btBulletDynamicsCommon.h>

namespace saba
{
	// WISTERIA deterministic-replay narrow interface. Exposes Bullet's
	// protected frame accumulator (m_localTime) so the engine can assert and
	// reset Canonical Frame Boundaries without tuning any solver parameter.
	class DeterministicDynamicsWorld final : public btDiscreteDynamicsWorld
	{
	public:
		using btDiscreteDynamicsWorld::btDiscreteDynamicsWorld;

		btScalar GetSimulationTime() const
		{
			return m_localTime;
		}

		void ResetSimulationTime()
		{
			m_localTime = btScalar(0);
		}
	};

	// WISTERIA deterministic-restore narrow interface for PMX joints: the
	// solver keeps warm-start impulses inside the 6DOF limit motors, which
	// the public API cannot clear. A subclass owned by Saba exposes the
	// reset so a restored world does not inherit joint impulse history.
	class SabaDeterministic6DofSpringConstraint final
		: public btGeneric6DofSpringConstraint
	{
	public:
		using btGeneric6DofSpringConstraint::btGeneric6DofSpringConstraint;

		void ResetAccumulatedImpulses()
		{
			m_linearLimits.m_accumulatedImpulse.setValue(0, 0, 0);
			for (int i = 0; i < 3; ++i)
			{
				m_angularLimits[i].m_accumulatedImpulse = 0;
			}
		}
	};

	class MMDMotionState : public btMotionState
	{
	public:
		virtual void Reset() = 0;
		virtual void ReflectGlobalTransform() = 0;
		virtual void SetTransform(const btTransform& transform) = 0;
	};

	namespace
	{
		glm::mat4 InvZ(const glm::mat4& m)
		{
			const glm::mat4 invZ = glm::scale(glm::mat4(1), glm::vec3(1, 1, -1));
			return invZ * m * invZ;
		}
	}

	struct MMDFilterCallback : public btOverlapFilterCallback
	{
		bool needBroadphaseCollision(btBroadphaseProxy* proxy0, btBroadphaseProxy* proxy1) const override
		{
			auto findIt = std::find_if(
				m_nonFilterProxy.begin(),
				m_nonFilterProxy.end(),
				[proxy0, proxy1](const auto& x) {return x == proxy0 || x == proxy1; }
			);
			if (findIt != m_nonFilterProxy.end())
			{
				return true;
			}
			bool collides = (proxy0->m_collisionFilterGroup & proxy1->m_collisionFilterMask) != 0;
			collides = collides && (proxy1->m_collisionFilterGroup & proxy0->m_collisionFilterMask);
			return collides;
		}

		std::vector<btBroadphaseProxy*> m_nonFilterProxy;
	};

	MMDPhysics::MMDPhysics()
		: m_fps(120.0f)
		, m_maxSubStepCount(10)
	{
	}

	MMDPhysics::~MMDPhysics()
	{
		Destroy();
	}

	bool MMDPhysics::Create()
	{
		// R1.2C deterministic continuation: use a history-free broadphase.
		// btDbvtBroadphase's tree/uid state depends on insertion history,
		// so even after a canonical rebuild the pair iteration order can
		// differ between from-start and restore on some platforms. Pair
		// enumeration by object index order is fully canonical.
		m_broadphase = std::make_unique<btSimpleBroadphase>();
		m_collisionConfig = std::make_unique<btDefaultCollisionConfiguration>();
		m_dispatcher = std::make_unique<btCollisionDispatcher>(m_collisionConfig.get());

		m_solver = std::make_unique<btSequentialImpulseConstraintSolver>();

		m_world = std::make_unique<DeterministicDynamicsWorld>(
			m_dispatcher.get(),
			m_broadphase.get(),
			m_solver.get(),
			m_collisionConfig.get()
			);

		m_world->setGravity(btVector3(0, -9.8f * 10.0f, 0));

		m_groundShape = std::make_unique<btStaticPlaneShape>(btVector3(0, 1, 0), 0.0f);

		btTransform groundTransform;
		groundTransform.setIdentity();

		m_groundMS = std::make_unique<btDefaultMotionState>(groundTransform);

		btRigidBody::btRigidBodyConstructionInfo groundInfo(0, m_groundMS.get(), m_groundShape.get(), btVector3(0, 0, 0));
		m_groundRB = std::make_unique<btRigidBody>(groundInfo);

		m_world->addRigidBody(m_groundRB.get());

		auto filterCB = std::make_unique<MMDFilterCallback>();
		filterCB->m_nonFilterProxy.push_back(m_groundRB->getBroadphaseProxy());
		m_world->getPairCache()->setOverlapFilterCallback(filterCB.get());
		m_filterCB = std::move(filterCB);

		return true;
	}

	void MMDPhysics::Destroy()
	{
		if (m_world != nullptr && m_groundRB != nullptr)
		{
			m_world->removeRigidBody(m_groundRB.get());
		}

		m_broadphase = nullptr;
		m_collisionConfig = nullptr;
		m_dispatcher = nullptr;
		m_solver = nullptr;
		m_world = nullptr;
		m_groundShape = nullptr;
		m_groundMS = nullptr;
		m_groundRB = nullptr;
	}

	void MMDPhysics::SetFPS(float fps)
	{
		m_fps = fps;
	}

	float MMDPhysics::GetFPS() const
	{
		return static_cast<float>(m_fps);
	}

	void MMDPhysics::SetMaxSubStepCount(int numSteps)
	{
		m_maxSubStepCount = numSteps;
	}

	int MMDPhysics::GetMaxSubStepCount() const
	{
		return m_maxSubStepCount;
	}


	int MMDPhysics::Update(float time)
	{
		if (m_world != nullptr)
		{
			return m_world->stepSimulation(
				time,
				m_maxSubStepCount,
				static_cast<btScalar>(1.0 / m_fps)
			);
		}
		return 0;
	}

	float MMDPhysics::GetSimulationTime() const
	{
		if (m_world == nullptr)
		{
			return 0.0f;
		}
		auto* deterministicWorld = static_cast<DeterministicDynamicsWorld*>(
			m_world.get()
		);
		return static_cast<float>(deterministicWorld->GetSimulationTime());
	}

	void MMDPhysics::ResetSimulationTime()
	{
		if (m_world == nullptr)
		{
			return;
		}
		auto* deterministicWorld = static_cast<DeterministicDynamicsWorld*>(
			m_world.get()
		);
		deterministicWorld->ResetSimulationTime();
	}

	void MMDPhysics::ClearContactManifoldsDeterministic()
	{
		if (m_world == nullptr)
		{
			return;
		}
		btDispatcher* dispatcher = m_world->getDispatcher();
		// Drop every existing manifold so accumulated contact impulses die
		// with them. This is the deterministic cold-step primitive.
		for (int i = dispatcher->getNumManifolds() - 1; i >= 0; --i)
		{
			dispatcher->clearManifold(
				dispatcher->getManifoldByIndexInternal(i)
			);
		}
	}

	void MMDPhysics::RebuildCollisionWorldDeterministic()
	{
		if (m_world == nullptr)
		{
			return;
		}
		btDispatcher* dispatcher = m_world->getDispatcher();
		// A deterministic collision world requires a canonical broadphase
		// tree: the internal tree shape depends on insertion history and can
		// change pair iteration order after a few frames. Re-insert every
		// collision object in stable index order so two histories converge.
		struct Entry
		{
			btCollisionObject* object;
			short group;
			short mask;
		};
		std::vector<Entry> entries;
		const btCollisionObjectArray& objects =
			m_world->getCollisionObjectArray();
		entries.reserve(objects.size());
		for (int i = 0; i < objects.size(); ++i)
		{
			Entry entry;
			entry.object = objects[i];
			entry.group = 1;
			entry.mask = -1;
			if (btBroadphaseProxy* proxy =
					objects[i]->getBroadphaseHandle())
			{
				entry.group = proxy->m_collisionFilterGroup;
				entry.mask = proxy->m_collisionFilterMask;
			}
			entries.push_back(entry);
		}
		// The current ground proxy is about to be destroyed; drop it from
		// the filter callback now so no stale pointer is consulted during
		// removal/rebuild. It is re-seated after all objects are re-added.
		MMDFilterCallback* filterCallback =
			static_cast<MMDFilterCallback*>(m_filterCB.get());
		if (filterCallback != nullptr)
		{
			filterCallback->m_nonFilterProxy.clear();
		}
		for (const Entry& entry : entries)
		{
			m_world->removeCollisionObject(entry.object);
		}
		// Canonical handle pool: without this, btSimpleBroadphase's LIFO
		// freeHandle flips the handle/object mapping on every rebuild, so
		// pair iteration order (by handle index) depends on rebuild-count
		// parity. Reset makes Rebuild an idempotent canonical operation.
		static_cast<btSimpleBroadphase*>(m_broadphase.get())
			->resetPool(dispatcher);
		// WISTERIA deterministic-restore: release every manifold and reset
		// the manifold/algorithm pool free lists so the re-added world
		// creates manifolds/algorithms in canonical pair-iteration order
		// independent of release history.
		static_cast<btCollisionDispatcher*>(dispatcher)
			->resetCollisionPools();
		for (const Entry& entry : entries)
		{
			// Rigid bodies must go through addRigidBody so they are
			// re-registered in m_nonStaticRigidBodies (integration list) and
			// receive world gravity; addCollisionObject alone would leave
			// dynamic bodies frozen.
			if (btRigidBody* body = btRigidBody::upcast(entry.object))
			{
				m_world->addRigidBody(body, entry.group, entry.mask);
			}
			else
			{
				m_world->addCollisionObject(
					entry.object,
					entry.group,
					entry.mask
				);
			}
		}
		// The ground proxy was destroyed and recreated above; the overlap
		// filter callback still references the old (now dangling) proxy.
		// Re-seat it so the ground keeps its unconditional non-filter
		// collision rule after a deterministic world rebuild.
		if (m_groundRB != nullptr && filterCallback != nullptr)
		{
			filterCallback->m_nonFilterProxy.clear();
			if (btBroadphaseProxy* proxy =
					m_groundRB->getBroadphaseHandle())
			{
				filterCallback->m_nonFilterProxy.push_back(proxy);
			}
		}
		m_world->updateAabbs();
		if (btBroadphaseInterface* broadphase = m_world->getBroadphase())
		{
			broadphase->calculateOverlappingPairs(dispatcher);
		}
	}

	void MMDPhysics::ClearSolverHistoryDeterministic()
	{
		if (m_world == nullptr)
		{
			return;
		}
		if (m_solver != nullptr)
		{
			if (auto* sequential = dynamic_cast<
					btSequentialImpulseConstraintSolver*>(m_solver.get()))
			{
				sequential->reset();
			}
		}
		for (int i = 0; i < m_world->getNumConstraints(); ++i)
		{
			btTypedConstraint* constraint = m_world->getConstraint(i);
			auto* deterministic = dynamic_cast<
				SabaDeterministic6DofSpringConstraint*>(constraint);
			if (deterministic != nullptr)
			{
				deterministic->ResetAccumulatedImpulses();
			}
		}
	}

	void MMDPhysics::AddRigidBody(MMDRigidBody * mmdRB)
	{
		m_world->addRigidBody(
			mmdRB->GetRigidBody(),
			1 << mmdRB->GetGroup(),
			mmdRB->GetGroupMask()
		);
	}

	void MMDPhysics::RemoveRigidBody(MMDRigidBody * mmdRB)
	{
		m_world->removeRigidBody(mmdRB->GetRigidBody());
	}

	void MMDPhysics::AddJoint(MMDJoint * mmdJoint)
	{
		if (mmdJoint->GetConstraint() != nullptr)
		{
			m_world->addConstraint(mmdJoint->GetConstraint());
		}
	}

	void MMDPhysics::RemoveJoint(MMDJoint * mmdJoint)
	{
		if (mmdJoint->GetConstraint() != nullptr)
		{
			m_world->removeConstraint(mmdJoint->GetConstraint());
		}
	}

	void MMDPhysics::SetLinkedBodyCollisionMode(MMDLinkedBodyCollisionMode mode)
	{
		m_linkedBodyCollisionMode = mode;
	}

	MMDLinkedBodyCollisionMode MMDPhysics::GetLinkedBodyCollisionMode() const
	{
		return m_linkedBodyCollisionMode;
	}

	void MMDPhysics::ApplyLinkedBodyCollisionMode()
	{
		if (m_world == nullptr)
		{
			return;
		}
		const bool disable =
			m_linkedBodyCollisionMode ==
			MMDLinkedBodyCollisionMode::DisableConstraintLinkedPairs;
		std::vector<btTypedConstraint*> constraints;
		constraints.reserve(m_world->getNumConstraints());
		for (int index = 0; index < m_world->getNumConstraints(); ++index)
		{
			constraints.push_back(m_world->getConstraint(index));
		}
		for (btTypedConstraint* constraint : constraints)
		{
			m_world->removeConstraint(constraint);
			m_world->addConstraint(constraint, disable);
		}
	}

	btDiscreteDynamicsWorld * MMDPhysics::GetDynamicsWorld() const
	{
		return m_world.get();
	}

	//*******************
	// MMDRigidBody
	//*******************

	class DefaultMotionState : public MMDMotionState
	{
	public:
		DefaultMotionState(const glm::mat4& transform)
		{
			glm::mat4 trans = InvZ(transform);
			m_transform.setFromOpenGLMatrix(&trans[0][0]);
			m_initialTransform = m_transform;
		}

		void getWorldTransform(btTransform& worldTransform) const override
		{
			worldTransform = m_transform;
		}

		void setWorldTransform(const btTransform& worldTransform) override
		{
			m_transform = worldTransform;
		}

		virtual void Reset() override
		{
			m_transform = m_initialTransform;
		}

		virtual void ReflectGlobalTransform() override
		{
		}

		virtual void SetTransform(const btTransform& transform) override
		{
			m_transform = transform;
		}


	private:
		btTransform	m_initialTransform;
		btTransform	m_transform;
	};

	class DynamicMotionState : public MMDMotionState
	{
	public:
		DynamicMotionState(MMDNode* node, const glm::mat4& offset, bool override = true)
			: m_node(node)
			, m_offset(offset)
			, m_override(override)
		{
			m_invOffset = glm::inverse(offset);
			Reset();
		}

		void getWorldTransform(btTransform& worldTransform) const override
		{
			worldTransform = m_transform;
		}

		void setWorldTransform(const btTransform& worldTransform) override
		{
			m_transform = worldTransform;
		}

		void Reset() override
		{
			glm::mat4 global = InvZ(m_node->GetGlobalTransform() * m_offset);
			m_transform.setFromOpenGLMatrix(&global[0][0]);
		}

		void ReflectGlobalTransform() override
		{
			alignas(16) glm::mat4 world;
			m_transform.getOpenGLMatrix(&world[0][0]);
			glm::mat4 btGlobal = InvZ(world) * m_invOffset;

			if (m_override)
			{
				m_node->SetGlobalTransform(btGlobal);
				m_node->UpdateChildTransform();
			}
		}

		virtual void SetTransform(const btTransform& transform) override
		{
			m_transform = transform;
		}

	private:
		MMDNode*	m_node;
		glm::mat4	m_offset;
		glm::mat4	m_invOffset;
		btTransform	m_transform;
		bool		m_override;
	};

	class DynamicAndBoneMergeMotionState : public MMDMotionState
	{
	public:
		DynamicAndBoneMergeMotionState(
			MMDNode* node,
			const glm::mat4& offset,
			bool override = true,
			bool preserveAnimatedTranslation = true
		)
			: m_node(node)
			, m_offset(offset)
			, m_override(override)
			, m_preserveAnimatedTranslation(preserveAnimatedTranslation)
		{
			m_invOffset = glm::inverse(offset);
			Reset();
		}

		void SetPreserveAnimatedTranslation(bool preserve)
		{
			m_preserveAnimatedTranslation = preserve;
		}

		void getWorldTransform(btTransform& worldTransform) const override
		{
			worldTransform = m_transform;
		}

		void setWorldTransform(const btTransform& worldTransform) override
		{
			m_transform = worldTransform;
		}

		void Reset() override
		{
			glm::mat4 global = InvZ(m_node->GetGlobalTransform() * m_offset);
			m_transform.setFromOpenGLMatrix(&global[0][0]);
		}

		void ReflectGlobalTransform() override
		{
			alignas(16) glm::mat4 world;
			m_transform.getOpenGLMatrix(&world[0][0]);
			glm::mat4 btGlobal = InvZ(world) * m_invOffset;
			glm::mat4 global = m_node->GetGlobalTransform();
			if (m_preserveAnimatedTranslation)
			{
				btGlobal[3] = global[3];
			}

			if (m_override)
			{
				m_node->SetGlobalTransform(btGlobal);
				m_node->UpdateChildTransform();
			}
		}

		virtual void SetTransform(const btTransform& transform) override
		{
			m_transform = transform;
		}

	private:
		MMDNode*	m_node;
		glm::mat4	m_offset;
		glm::mat4	m_invOffset;
		btTransform	m_transform;
		bool		m_override;
		bool		m_preserveAnimatedTranslation;

	};

	class KinematicMotionState : public MMDMotionState
	{
	public:
		KinematicMotionState(MMDNode* node, const glm::mat4& offset)
			: m_node(node)
			, m_offset(offset)
		{
		}

		void getWorldTransform(btTransform& worldTransform) const override
		{
			glm::mat4 m;
			if (m_node != nullptr)
			{
				m = m_node->GetGlobalTransform() * m_offset;
			}
			else
			{
				m = m_offset;
			}
			m = InvZ(m);
			worldTransform.setFromOpenGLMatrix(&m[0][0]);
		}

		void setWorldTransform(const btTransform& worldTransform) override
		{
		}

		void Reset() override
		{
		}

		void ReflectGlobalTransform() override
		{
		}

		virtual void SetTransform(const btTransform&) override
		{
			// Kinematic motion state derives its transform from the animated
			// node on every read; nothing to store.
		}

	private:
		MMDNode*	m_node;
		glm::mat4	m_offset;
	};

	MMDRigidBody::MMDRigidBody()
		: m_rigidBodyType(RigidBodyType::Kinematic)
		, m_group(0)
		, m_groupMask(0)
		, m_node(0)
		, m_offsetMat(1)
		, m_boneIndex(-1)
		, m_definitionMass(0.0f)
	{
	}

	MMDRigidBody::~MMDRigidBody()
	{
	}

	bool MMDRigidBody::Create(const PMDRigidBodyExt& pmdRigidBody, MMDModel* model, MMDNode* node)
	{
		Destroy();

		switch (pmdRigidBody.m_shapeType)
		{
		case PMDRigidBodyShape::Sphere:
			m_shape = std::make_unique<btSphereShape>(pmdRigidBody.m_shapeWidth);
			break;
		case PMDRigidBodyShape::Box:
			m_shape = std::make_unique<btBoxShape>(btVector3(
				pmdRigidBody.m_shapeWidth,
				pmdRigidBody.m_shapeHeight,
				pmdRigidBody.m_shapeDepth
			));
			break;
		case PMDRigidBodyShape::Capsule:
			m_shape = std::make_unique<btCapsuleShape>(
				pmdRigidBody.m_shapeWidth,
				pmdRigidBody.m_shapeHeight
				);
			break;
		default:
			break;
		}
		if (m_shape == nullptr)
		{
			return false;
		}

		btScalar mass(0.0f);
		btVector3 localInteria(0, 0, 0);
		if (pmdRigidBody.m_rigidBodyType != PMDRigidBodyOperation::Static)
		{
			mass = pmdRigidBody.m_rigidBodyWeight;
		}
		if (mass != 0)
		{
			m_shape->calculateLocalInertia(mass, localInteria);
		}

		auto rx = glm::rotate(glm::mat4(1), pmdRigidBody.m_rot.x, glm::vec3(1, 0, 0));
		auto ry = glm::rotate(glm::mat4(1), pmdRigidBody.m_rot.y, glm::vec3(0, 1, 0));
		auto rz = glm::rotate(glm::mat4(1), pmdRigidBody.m_rot.z, glm::vec3(0, 0, 1));
		glm::mat4 rotMat = ry * rx * rz;
		glm::mat4 translateMat = glm::translate(glm::mat4(1), pmdRigidBody.m_pos);

		glm::mat4 rbMat = translateMat * rotMat;
		if (node != nullptr)
		{
			glm::mat4 global = node->GetGlobalTransform();
			rbMat = InvZ(global) * rbMat;
		}
		else
		{
			MMDNode* root = model->GetNodeManager()->GetMMDNode(0);
			glm::mat4 global = root->GetGlobalTransform();
			rbMat = InvZ(global) * rbMat;
		}
		rbMat = InvZ(rbMat);

		if (node != nullptr)
		{
			m_offsetMat = glm::inverse(node->GetGlobalTransform()) * rbMat;
		}
		else
		{
			MMDNode* root = model->GetNodeManager()->GetMMDNode(0);
			m_offsetMat = glm::inverse(root->GetGlobalTransform()) * rbMat;
		}

		btMotionState* motionState = nullptr;
		MMDNode* kinematicNode = nullptr;
		bool overrideNode = true;
		if (node != nullptr)
		{
			kinematicNode = node;
		}
		else
		{
			kinematicNode = model->GetNodeManager()->GetMMDNode(0);
			overrideNode = false;
		}
		if (pmdRigidBody.m_rigidBodyType == PMDRigidBodyOperation::Static)
		{
			m_kinematicMotionState = std::make_unique<KinematicMotionState>(kinematicNode, m_offsetMat);
			motionState = m_kinematicMotionState.get();
		}
		else if (pmdRigidBody.m_rigidBodyType == PMDRigidBodyOperation::Dynamic)
		{
			m_activeMotionState = std::make_unique<DynamicMotionState>(kinematicNode, m_offsetMat, overrideNode);
			m_kinematicMotionState = std::make_unique<KinematicMotionState>(kinematicNode, m_offsetMat);
			motionState = m_activeMotionState.get();
		}
		else if (pmdRigidBody.m_rigidBodyType == PMDRigidBodyOperation::DynamicAdjustBone)
		{
			m_activeMotionState = std::make_unique<DynamicAndBoneMergeMotionState>(
				kinematicNode,
				m_offsetMat,
				overrideNode,
				m_mode2PreserveTranslation
			);
			m_kinematicMotionState = std::make_unique<KinematicMotionState>(kinematicNode, m_offsetMat);
			motionState = m_activeMotionState.get();
		}

		btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, m_shape.get(), localInteria);
		rbInfo.m_linearDamping = pmdRigidBody.m_rigidBodyPosDimmer;
		rbInfo.m_angularDamping = pmdRigidBody.m_rigidBodyRotDimmer;
		rbInfo.m_restitution = pmdRigidBody.m_rigidBodyRecoil;
		rbInfo.m_friction = pmdRigidBody.m_rigidBodyFriction;
		rbInfo.m_additionalDamping = true;

		m_rigidBody = std::make_unique<btRigidBody>(rbInfo);
		m_rigidBody->setUserPointer(this);
		m_rigidBody->setSleepingThresholds(0.01f, glm::radians(0.1f));
		m_rigidBody->setActivationState(DISABLE_DEACTIVATION);
		if (pmdRigidBody.m_rigidBodyType == PMDRigidBodyOperation::Static)
		{
			m_rigidBody->setCollisionFlags(m_rigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
		}

		m_rigidBodyType = (RigidBodyType)pmdRigidBody.m_rigidBodyType;
		m_group = pmdRigidBody.m_groupIndex;
		m_groupMask = pmdRigidBody.m_groupTarget;
		m_node = node;
		m_definitionMass = pmdRigidBody.m_rigidBodyWeight;
		m_boneIndex = node != nullptr
			? static_cast<int32_t>(node->GetIndex())
			: static_cast<int32_t>(
				  model->GetNodeManager()->GetMMDNode(0)->GetIndex()
			  );
		m_name = pmdRigidBody.m_rigidBodyName.ToUtf8String();

		return true;
	}

	bool MMDRigidBody::Create(const PMXRigidbody & pmxRigidBody, MMDModel* model, MMDNode * node)
	{
		Destroy();

		switch (pmxRigidBody.m_shape)
		{
		case PMXRigidbody::Shape::Sphere:
			m_shape = std::make_unique<btSphereShape>(pmxRigidBody.m_shapeSize.x);
			break;
		case PMXRigidbody::Shape::Box:
			m_shape = std::make_unique<btBoxShape>(btVector3(
				pmxRigidBody.m_shapeSize.x,
				pmxRigidBody.m_shapeSize.y,
				pmxRigidBody.m_shapeSize.z
			));
			break;
		case PMXRigidbody::Shape::Capsule:
			m_shape = std::make_unique<btCapsuleShape>(
				pmxRigidBody.m_shapeSize.x,
				pmxRigidBody.m_shapeSize.y
				);
			break;
		default:
			break;
		}
		if (m_shape == nullptr)
		{
			return false;
		}

		btScalar mass(0.0f);
		btVector3 localInteria(0, 0, 0);
		if (pmxRigidBody.m_op != PMXRigidbody::Operation::Static)
		{
			mass = pmxRigidBody.m_mass;
		}
		if (mass != 0)
		{
			m_shape->calculateLocalInertia(mass, localInteria);
		}

		auto rx = glm::rotate(glm::mat4(1), pmxRigidBody.m_rotate.x, glm::vec3(1, 0, 0));
		auto ry = glm::rotate(glm::mat4(1), pmxRigidBody.m_rotate.y, glm::vec3(0, 1, 0));
		auto rz = glm::rotate(glm::mat4(1), pmxRigidBody.m_rotate.z, glm::vec3(0, 0, 1));
		glm::mat4 rotMat = ry * rx * rz;
		glm::mat4 translateMat = glm::translate(glm::mat4(1), pmxRigidBody.m_translate);

		glm::mat4 rbMat = InvZ(translateMat * rotMat);

		MMDNode* kinematicNode = nullptr;
		if (node != nullptr)
		{
			m_offsetMat = glm::inverse(node->GetGlobalTransform()) * rbMat;
			kinematicNode = node;
		}
		else
		{
			MMDNode* root = model->GetNodeManager()->GetMMDNode(0);
			m_offsetMat = glm::inverse(root->GetGlobalTransform()) * rbMat;
			kinematicNode = root;
		}

		btMotionState* MMDMotionState = nullptr;
		if (pmxRigidBody.m_op == PMXRigidbody::Operation::Static)
		{
			m_kinematicMotionState = std::make_unique<KinematicMotionState>(kinematicNode, m_offsetMat);
			MMDMotionState = m_kinematicMotionState.get();
		}
		else
		{
			if (node != nullptr)
			{
				if (pmxRigidBody.m_op == PMXRigidbody::Operation::Dynamic)
				{
					m_activeMotionState = std::make_unique<DynamicMotionState>(kinematicNode, m_offsetMat);
					m_kinematicMotionState = std::make_unique<KinematicMotionState>(kinematicNode, m_offsetMat);
					MMDMotionState = m_activeMotionState.get();
				}
				else if (pmxRigidBody.m_op == PMXRigidbody::Operation::DynamicAndBoneMerge)
				{
					m_activeMotionState = std::make_unique<DynamicAndBoneMergeMotionState>(
						kinematicNode,
						m_offsetMat,
						true,
						m_mode2PreserveTranslation
					);
					m_kinematicMotionState = std::make_unique<KinematicMotionState>(kinematicNode, m_offsetMat);
					MMDMotionState = m_activeMotionState.get();
				}
			}
			else
			{
				m_activeMotionState = std::make_unique<DefaultMotionState>(m_offsetMat);
				m_kinematicMotionState = std::make_unique<KinematicMotionState>(kinematicNode, m_offsetMat);
				MMDMotionState = m_activeMotionState.get();
			}
		}

		btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, MMDMotionState, m_shape.get(), localInteria);
		rbInfo.m_linearDamping = pmxRigidBody.m_translateDimmer;
		rbInfo.m_angularDamping = pmxRigidBody.m_rotateDimmer;
		rbInfo.m_restitution = pmxRigidBody.m_repulsion;
		rbInfo.m_friction = pmxRigidBody.m_friction;
		rbInfo.m_additionalDamping = true;

		m_rigidBody = std::make_unique<btRigidBody>(rbInfo);
		m_rigidBody->setUserPointer(this);
		m_rigidBody->setSleepingThresholds(0.01f, glm::radians(0.1f));
		m_rigidBody->setActivationState(DISABLE_DEACTIVATION);
		if (pmxRigidBody.m_op == PMXRigidbody::Operation::Static)
		{
			m_rigidBody->setCollisionFlags(m_rigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
		}

		m_rigidBodyType = (RigidBodyType)pmxRigidBody.m_op;
		m_group = pmxRigidBody.m_group;
		m_groupMask = pmxRigidBody.m_collisionGroup;
		m_node = node;
		m_definitionMass = pmxRigidBody.m_mass;
		m_boneIndex = node != nullptr
			? static_cast<int32_t>(node->GetIndex())
			: static_cast<int32_t>(
				  model->GetNodeManager()->GetMMDNode(0)->GetIndex()
			  );
		m_name = pmxRigidBody.m_name;

		return true;
	}

	void MMDRigidBody::Destroy()
	{
		m_shape = nullptr;
	}

	btRigidBody * MMDRigidBody::GetRigidBody() const
	{
		return m_rigidBody.get();
	}

	uint16_t MMDRigidBody::GetGroup() const
	{
		return m_group;
	}

	uint16_t MMDRigidBody::GetGroupMask() const
	{
		return m_groupMask;
	}

	int MMDRigidBody::GetRigidBodyType() const
	{
		return static_cast<int>(m_rigidBodyType);
	}

	int32_t MMDRigidBody::GetBoneIndex() const
	{
		return m_boneIndex;
	}

	const glm::mat4& MMDRigidBody::GetOffsetMatrix() const
	{
		return m_offsetMat;
	}

	float MMDRigidBody::GetDefinitionMass() const
	{
		return m_definitionMass;
	}

	void MMDRigidBody::SelectMotionStateForMode(int mode)
	{
		// mode 0 = FollowBone (kinematic), 1/2 = dynamic.
		SetActivation(mode != 0);
	}

	void MMDRigidBody::NormalizeCanonicalActivation(int mode)
	{
		// Motion-state selection happened once in Phase 1; re-selecting here
		// would read the (possibly stale) motion state back into the body
		// transform. This method only normalizes activation/deactivation.
		(void)mode;
		if (m_rigidBody == nullptr)
		{
			return;
		}
		// forceActivationState bypasses the DISABLE_DEACTIVATION guard that
		// plain setActivationState would honor; activate(true) then ensures
		// ACTIVE_TAG on the next simulation step.
		m_rigidBody->forceActivationState(ACTIVE_TAG);
		m_rigidBody->setDeactivationTime(0);
		m_rigidBody->activate(true);
	}

	void MMDRigidBody::SyncActiveMotionStateToBodyTransform()
	{
		if (m_activeMotionState == nullptr || m_rigidBody == nullptr)
		{
			return;
		}
		m_activeMotionState->SetTransform(
			m_rigidBody->getCenterOfMassTransform()
		);
	}

	void MMDRigidBody::SyncActiveMotionStateToTransform(
		const btTransform& transform
	)
	{
		if (m_activeMotionState == nullptr || m_rigidBody == nullptr)
		{
			return;
		}
		m_activeMotionState->SetTransform(transform);
	}

	void MMDRigidBody::SetActivation(bool activation)
	{
		if (m_rigidBodyType != RigidBodyType::Kinematic)
		{
			if (activation)
			{
				m_rigidBody->setCollisionFlags(m_rigidBody->getCollisionFlags() & ~btCollisionObject::CF_KINEMATIC_OBJECT);
				m_rigidBody->setMotionState(m_activeMotionState.get());
			}
			else
			{
				m_rigidBody->setCollisionFlags(m_rigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
				m_rigidBody->setMotionState(m_kinematicMotionState.get());
			}
		}
		else
		{
			m_rigidBody->setMotionState(m_kinematicMotionState.get());
		}
	}

	void MMDRigidBody::ResetTransform()
	{
		if (m_activeMotionState != nullptr)
		{
			m_activeMotionState->Reset();
		}
	}

	void MMDRigidBody::Reset(MMDPhysics* physics)
	{
		auto cache = physics->GetDynamicsWorld()->getPairCache();
		if (cache != nullptr)
		{
			auto dispatcher = physics->GetDynamicsWorld()->getDispatcher();
			cache->cleanProxyFromPairs(m_rigidBody->getBroadphaseHandle(), dispatcher);
		}
		m_rigidBody->setAngularVelocity(btVector3(0, 0, 0));
		m_rigidBody->setLinearVelocity(btVector3(0, 0, 0));
		m_rigidBody->clearForces();
	}

	void MMDRigidBody::ReflectGlobalTransform()
	{
		if (m_activeMotionState != nullptr)
		{
			m_activeMotionState->ReflectGlobalTransform();
		}
		if (m_kinematicMotionState != nullptr)
		{
			m_kinematicMotionState->ReflectGlobalTransform();
		}
	}

	void MMDRigidBody::CalcLocalTransform()
	{
		if (m_node != nullptr)
		{
			auto parent = m_node->GetParent();
			if (parent != nullptr)
			{
				auto local = glm::inverse(parent->GetGlobalTransform()) * m_node->GetGlobalTransform();
				m_node->SetLocalTransform(local);
			}
			else
			{
				m_node->SetLocalTransform(m_node->GetGlobalTransform());
			}
		}
	}

	glm::mat4 MMDRigidBody::GetTransform()
	{
		btTransform transform = m_rigidBody->getCenterOfMassTransform();
		alignas(16) glm::mat4 mat;
		transform.getOpenGLMatrix(&mat[0][0]);
		return InvZ(mat);
	}


	//*******************
	// MMDJoint
	//*******************
	MMDJoint::MMDJoint()
	{
	}

	MMDJoint::~MMDJoint()
	{
	}

	bool MMDJoint::CreateJoint(const PMDJointExt& pmdJoint, MMDRigidBody* rigidBodyA, MMDRigidBody* rigidBodyB)
	{
		Destroy();

		btMatrix3x3 rotMat;
		rotMat.setEulerZYX(pmdJoint.m_jointRot.x, pmdJoint.m_jointRot.y, pmdJoint.m_jointRot.z);

		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(btVector3(
			pmdJoint.m_jointPos.x,
			pmdJoint.m_jointPos.y,
			pmdJoint.m_jointPos.z
		));
		transform.setBasis(rotMat);

		btTransform invA = rigidBodyA->GetRigidBody()->getWorldTransform().inverse();
		btTransform invB = rigidBodyB->GetRigidBody()->getWorldTransform().inverse();
		invA = invA * transform;
		invB = invB * transform;

		auto constraint = std::make_unique<SabaDeterministic6DofSpringConstraint>(
			*rigidBodyA->GetRigidBody(),
			*rigidBodyB->GetRigidBody(),
			invA,
			invB,
			true);
		constraint->setLinearLowerLimit(btVector3(
			pmdJoint.m_constrainPos1.x,
			pmdJoint.m_constrainPos1.y,
			pmdJoint.m_constrainPos1.z
		));
		constraint->setLinearUpperLimit(btVector3(
			pmdJoint.m_constrainPos2.x,
			pmdJoint.m_constrainPos2.y,
			pmdJoint.m_constrainPos2.z
		));

		constraint->setAngularLowerLimit(btVector3(
			pmdJoint.m_constrainRot1.x,
			pmdJoint.m_constrainRot1.y,
			pmdJoint.m_constrainRot1.z
		));
		constraint->setAngularUpperLimit(btVector3(
			pmdJoint.m_constrainRot2.x,
			pmdJoint.m_constrainRot2.y,
			pmdJoint.m_constrainRot2.z
		));

		if (pmdJoint.m_springPos.x != 0)
		{
			constraint->enableSpring(0, true);
			constraint->setStiffness(0, pmdJoint.m_springPos.x);
		}
		if (pmdJoint.m_springPos.y != 0)
		{
			constraint->enableSpring(1, true);
			constraint->setStiffness(1, pmdJoint.m_springPos.y);
		}
		if (pmdJoint.m_springPos.z != 0)
		{
			constraint->enableSpring(2, true);
			constraint->setStiffness(2, -pmdJoint.m_springPos.z);
		}
		if (pmdJoint.m_springRot.x != 0)
		{
			constraint->enableSpring(3, true);
			constraint->setStiffness(3, pmdJoint.m_springRot.x);
		}
		if (pmdJoint.m_springRot.y != 0)
		{
			constraint->enableSpring(4, true);
			constraint->setStiffness(4, pmdJoint.m_springRot.y);
		}
		if (pmdJoint.m_springRot.z != 0)
		{
			constraint->enableSpring(5, true);
			constraint->setStiffness(5, pmdJoint.m_springRot.z);
		}

		m_constraint = std::move(constraint);

		return true;
	}

	bool MMDJoint::CreateJoint(const PMXJoint& pmxJoint, MMDRigidBody* rigidBodyA, MMDRigidBody* rigidBodyB)
	{
		Destroy();

		btMatrix3x3 rotMat;
		rotMat.setEulerZYX(pmxJoint.m_rotate.x, pmxJoint.m_rotate.y, pmxJoint.m_rotate.z);

		btTransform transform;
		transform.setIdentity();
		transform.setOrigin(btVector3(
			pmxJoint.m_translate.x,
			pmxJoint.m_translate.y,
			pmxJoint.m_translate.z
		));
		transform.setBasis(rotMat);

		btTransform invA = rigidBodyA->GetRigidBody()->getWorldTransform().inverse();
		btTransform invB = rigidBodyB->GetRigidBody()->getWorldTransform().inverse();
		invA = invA * transform;
		invB = invB * transform;

		auto constraint = std::make_unique<SabaDeterministic6DofSpringConstraint>(
			*rigidBodyA->GetRigidBody(),
			*rigidBodyB->GetRigidBody(),
			invA,
			invB,
			true);
		constraint->setLinearLowerLimit(btVector3(
			pmxJoint.m_translateLowerLimit.x,
			pmxJoint.m_translateLowerLimit.y,
			pmxJoint.m_translateLowerLimit.z
		));
		constraint->setLinearUpperLimit(btVector3(
			pmxJoint.m_translateUpperLimit.x,
			pmxJoint.m_translateUpperLimit.y,
			pmxJoint.m_translateUpperLimit.z
		));

		constraint->setAngularLowerLimit(btVector3(
			pmxJoint.m_rotateLowerLimit.x,
			pmxJoint.m_rotateLowerLimit.y,
			pmxJoint.m_rotateLowerLimit.z
		));
		constraint->setAngularUpperLimit(btVector3(
			pmxJoint.m_rotateUpperLimit.x,
			pmxJoint.m_rotateUpperLimit.y,
			pmxJoint.m_rotateUpperLimit.z
		));

		if (pmxJoint.m_springTranslateFactor.x != 0)
		{
			constraint->enableSpring(0, true);
			constraint->setStiffness(0, pmxJoint.m_springTranslateFactor.x);
		}
		if (pmxJoint.m_springTranslateFactor.y != 0)
		{
			constraint->enableSpring(1, true);
			constraint->setStiffness(1, pmxJoint.m_springTranslateFactor.y);
		}
		if (pmxJoint.m_springTranslateFactor.z != 0)
		{
			constraint->enableSpring(2, true);
			constraint->setStiffness(2, pmxJoint.m_springTranslateFactor.z);
		}
		if (pmxJoint.m_springRotateFactor.x != 0)
		{
			constraint->enableSpring(3, true);
			constraint->setStiffness(3, pmxJoint.m_springRotateFactor.x);
		}
		if (pmxJoint.m_springRotateFactor.y != 0)
		{
			constraint->enableSpring(4, true);
			constraint->setStiffness(4, pmxJoint.m_springRotateFactor.y);
		}
		if (pmxJoint.m_springRotateFactor.z != 0)
		{
			constraint->enableSpring(5, true);
			constraint->setStiffness(5, pmxJoint.m_springRotateFactor.z);
		}

		m_constraint = std::move(constraint);

		return true;
	}

	void MMDJoint::Destroy()
	{
		m_constraint = nullptr;
	}

	btTypedConstraint * MMDJoint::GetConstraint() const
	{
		return m_constraint.get();
	}

	void MMDRigidBody::SetMode2PreserveTranslation(bool preserve)
	{
		m_mode2PreserveTranslation = preserve;
		auto* merge = dynamic_cast<DynamicAndBoneMergeMotionState*>(
			m_activeMotionState.get());
		if (merge != nullptr)
		{
			merge->SetPreserveAnimatedTranslation(preserve);
		}
	}

	bool MMDRigidBody::GetMode2PreserveTranslation() const
	{
		return m_mode2PreserveTranslation;
	}

	void MMDJoint::ResetConstraintImpulses()
	{
		if (m_constraint == nullptr)
		{
			return;
		}
		auto* deterministic = dynamic_cast<
			SabaDeterministic6DofSpringConstraint*>(m_constraint.get());
		if (deterministic != nullptr)
		{
			deterministic->ResetAccumulatedImpulses();
		}
	}

}
