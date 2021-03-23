/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2010,2011,2012,2013 TELEMATICS LAB, Politecnico di Bari
 *
 * This file is part of LTE-Sim
 *
 * LTE-Sim is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation;
 *
 * LTE-Sim is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LTE-Sim; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Giuseppe Piro <g.piro@poliba.it>
 */

#include "proposed-downlink-packet-scheduler.h"
#include "../mac-entity.h"
#include "../../packet/Packet.h"
#include "../../packet/packet-burst.h"
#include "../../../device/NetworkNode.h"
#include "../../../flows/radio-bearer.h"
#include "../../../protocolStack/rrc/rrc-entity.h"
#include "../../../flows/application/Application.h"
#include "../../../device/ENodeB.h"
#include "../../../protocolStack/mac/AMCModule.h"
#include "../../../phy/lte-phy.h"
#include "../../../core/spectrum/bandwidth-manager.h"
#include "../../../flows/QoS/QoSParameters.h"
#include "../../../flows/MacQueue.h"
#include "../../../utility/eesm-effective-sinr.h"

PROPOSEDDownlinkPacketScheduler::PROPOSEDDownlinkPacketScheduler()
{
	SetMacEntity(0);
	CreateFlowsToSchedule();
}

PROPOSEDDownlinkPacketScheduler::~PROPOSEDDownlinkPacketScheduler()
{
	Destroy();
}

void PROPOSEDDownlinkPacketScheduler::DoSchedule()
{
#ifdef SCHEDULER_DEBUG
	std::cout << "Start PROPOSED DL packet scheduler for node "
			  << GetMacEntity()->GetDevice()->GetIDNetworkNode() << std::endl;
#endif

	UpdateAverageTransmissionRate();
	CheckForDLDropPackets();
	SelectFlowsToSchedule();
	ComputeAverageOfHOLDelays();

	if (GetFlowsToSchedule()->size() == 0)
	{
	}
	else
	{
		RBsAllocation();
	}
	StopSchedule();
}

void PROPOSEDDownlinkPacketScheduler::ComputeAverageOfHOLDelays(void)
{
	double avgHOL = 0.;
	int nbFlows = 0;
	FlowsToSchedule *flowsToSchedule = GetFlowsToSchedule();
	FlowsToSchedule::iterator iter;
	FlowToSchedule *flow;

	for (iter = flowsToSchedule->begin(); iter != flowsToSchedule->end(); iter++)
	{
		flow = (*iter);
		if (flow->GetBearer()->HasPackets())
		{
			if ((flow->GetBearer()->GetApplication()->GetApplicationType() == Application::APPLICATION_TYPE_TRACE_BASED) ||
				(flow->GetBearer()->GetApplication()->GetApplicationType() == Application::APPLICATION_TYPE_VOIP))
			{
				avgHOL += flow->GetBearer()->GetHeadOfLinePacketDelay();
				nbFlows++;
			}
		}
	}

	m_avgHOLDelayes = avgHOL / nbFlows;
}
double
PROPOSEDDownlinkPacketScheduler::ComputeSchedulingMetric(RadioBearer *bearer, double spectralEfficiency, int subChannel)
{

#ifdef SCHEDULER_DEBUG
	std::cout << "\t ComputeSchedulingMetric for flow "
			  << bearer->GetApplication()->GetApplicationID() << std::endl;
#endif

	double metric;
	if ((bearer->GetApplication()->GetApplicationType() == Application::APPLICATION_TYPE_INFINITE_BUFFER) ||
		(bearer->GetApplication()->GetApplicationType() == Application::APPLICATION_TYPE_CBR))
	{
		metric = (spectralEfficiency * 180000.) /
				 bearer->GetAverageTransmissionRate();
/*
#ifdef SCHEDULER_DEBUG
		std::cout << "\t\t non real time flow: metric(PF) = " << metric << std::endl;
#endif
*/
	}
	else
	{
		QoSParameters *qos = bearer->GetQoSParameters();
		double HOL = bearer->GetHeadOfLinePacketDelay();
		double targetDelay = qos->GetMaxDelay();

		//COMPUTE METRIC USING EXP RULE:
		double numerator = (6 / targetDelay) * HOL;
		double denominator = (1 + sqrt(m_avgHOLDelayes));
		double weight = (spectralEfficiency * 180000.) /
						bearer->GetAverageTransmissionRate();

		metric = (exp(numerator / denominator)) * weight;
/*
#ifdef SCHEDULER_DEBUG
		std::cout << "\t\t real time flow: "
					 "\n\t\t\t HOL = "
				  << HOL << "\n\t\t\t target delay = " << targetDelay << "\n\t\t\t m_avgHOLDelayes = " << m_avgHOLDelayes << "\n\t\t\t spectralEfficiency = " << spectralEfficiency << "\n\t\t\t avg rate = " << bearer->GetAverageTransmissionRate() << "\n\t\t\t numerator " << numerator << "\n\t\t\t denominator " << denominator << "\n\t\t\t weight = " << weight << "\n\t\t --> metric = " << metric << std::endl;
#endif
*/
	}

	return metric;
}

void PROPOSEDDownlinkPacketScheduler::ComputeFairindex()
{

	FlowToSchedule *scheduledFlow;
	FlowsToSchedule::iterator iter;
	FlowToSchedule *flow;

	fairindex = 0.0;
	int nbFlow = 0;
}

void PROPOSEDDownlinkPacketScheduler::RBsAllocation()
{
#ifdef SCHEDULER_DEBUG
	std::cout << " ---- PROPOSEDDownlinkPacketScheduler::RBsAllocation\n";
#endif

	FlowsToSchedule *flows = GetFlowsToSchedule();
	int nbOfRBs = GetMacEntity()->GetDevice()->GetPhy()->GetBandwidthManager()->GetDlSubChannels().size();

	//create a matrix of flow metrics
	double metrics[nbOfRBs][flows->size()];
	for (int i = 0; i < nbOfRBs; i++)
	{
		for (int j = 0; j < flows->size(); j++)
		{
			metrics[i][j] = ComputeSchedulingMetric(flows->at(j)->GetBearer(),
													flows->at(j)->GetSpectralEfficiency().at(i),
													i);
		}
	}

#ifdef SCHEDULER_DEBUG
	std::cout << ", available RBs " << nbOfRBs << ", flows " << flows->size() << std::endl;
	for (int ii = 0; ii < flows->size(); ii++)
	{
		std::cout << "\t metrics for flow "
				  << flows->at(ii)->GetBearer()->GetApplication()->GetApplicationID() << ":";
		for (int jj = 0; jj < nbOfRBs; jj++)
		{
			std::cout << " " << metrics[jj][ii];
		}
		std::cout << std::endl;
	}
#endif

	AMCModule *amc = GetMacEntity()->GetAmcModule();
	double l_dAllocatedRBCounter = 0;

	int l_iNumberOfUsers = ((ENodeB *)this->GetMacEntity()->GetDevice())->GetNbOfUserEquipmentRecords();

	bool *l_bFlowScheduled = new bool[flows->size()];
	int l_iScheduledFlows = 0;
	std::vector<double> *l_bFlowScheduledSINR = new std::vector<double>[flows->size()];
	for (int k = 0; k < flows->size(); k++)
		l_bFlowScheduled[k] = false;

	//RBs allocation
	for (int s = 0; s < nbOfRBs; s++)
	{
		if (l_iScheduledFlows == flows->size())
			break;

		double targetMetric = 0;
		bool RBIsAllocated = false;
		FlowToSchedule *scheduledFlow;
		int l_iScheduledFlowIndex = 0;

		for (int k = 0; k < flows->size(); k++)
		{
			if (metrics[s][k] > targetMetric && !l_bFlowScheduled[k])
			{
				targetMetric = metrics[s][k];
				RBIsAllocated = true;
				scheduledFlow = flows->at(k);
				l_iScheduledFlowIndex = k;
			}
		}

		if (RBIsAllocated)
		{
			l_dAllocatedRBCounter++;

			scheduledFlow->GetListOfAllocatedRBs()->push_back(s); // the s RB has been allocated to that flow!

#ifdef SCHEDULER_DEBUG
			std::cout << "\t *** RB " << s << " assigned to the "
											  " flow "
					  << scheduledFlow->GetBearer()->GetApplication()->GetApplicationID()
					  << std::endl;
#endif
			double sinr = amc->GetSinrFromCQI(scheduledFlow->GetCqiFeedbacks().at(s));
			l_bFlowScheduledSINR[l_iScheduledFlowIndex].push_back(sinr);

			double effectiveSinr = GetEesmEffectiveSinr(l_bFlowScheduledSINR[l_iScheduledFlowIndex]);
			int mcs = amc->GetMCSFromCQI(amc->GetCQIFromSinr(effectiveSinr));
			int transportBlockSize = amc->GetTBSizeFromMCS(mcs, scheduledFlow->GetListOfAllocatedRBs()->size());
			if (transportBlockSize >= scheduledFlow->GetDataToTransmit() * 8)
			{
				l_bFlowScheduled[l_iScheduledFlowIndex] = true;
				l_iScheduledFlows++;
			}
		}
	}

	delete[] l_bFlowScheduled;
	delete[] l_bFlowScheduledSINR;

	//Finalize the allocation
	PdcchMapIdealControlMessage *pdcchMsg = new PdcchMapIdealControlMessage();

	for (FlowsToSchedule::iterator it = flows->begin(); it != flows->end(); it++)
	{
		FlowToSchedule *flow = (*it);
		if (flow->GetListOfAllocatedRBs()->size() > 0)
		{
			//this flow has been scheduled
			std::vector<double> estimatedSinrValues;
			for (int rb = 0; rb < flow->GetListOfAllocatedRBs()->size(); rb++)

			{
				double sinr = amc->GetSinrFromCQI(
					flow->GetCqiFeedbacks().at(flow->GetListOfAllocatedRBs()->at(rb)));

				estimatedSinrValues.push_back(sinr);
			}

			//compute the effective sinr
			double effectiveSinr = GetEesmEffectiveSinr(estimatedSinrValues);

			//get the MCS for transmission

			int mcs = amc->GetMCSFromCQI(amc->GetCQIFromSinr(effectiveSinr));

			//define the amount of bytes to transmit
			//int transportBlockSize = amc->GetTBSizeFromMCS (mcs);
			int transportBlockSize = amc->GetTBSizeFromMCS(mcs, flow->GetListOfAllocatedRBs()->size());
			double bitsToTransmit = transportBlockSize;
			flow->UpdateAllocatedBits(bitsToTransmit);

#ifdef SCHEDULER_DEBUG
			std::cout << "\t\t --> flow " << flow->GetBearer()->GetApplication()->GetApplicationID()
					  << " has been scheduled: "
					  << "\n\t\t\t nb of RBs " << flow->GetListOfAllocatedRBs()->size() << "\n\t\t\t effectiveSinr " << effectiveSinr << "\n\t\t\t tbs " << transportBlockSize << "\n\t\t\t bitsToTransmit " << bitsToTransmit
					  << std::endl;
#endif

			//create PDCCH messages
			for (int rb = 0; rb < flow->GetListOfAllocatedRBs()->size(); rb++)
			{
				pdcchMsg->AddNewRecord(PdcchMapIdealControlMessage::DOWNLINK,
									   flow->GetListOfAllocatedRBs()->at(rb),
									   flow->GetBearer()->GetDestination(),
									   mcs);
			}
		}
	}

	if (pdcchMsg->GetMessage()->size() > 0)
	{
		GetMacEntity()->GetDevice()->GetPhy()->SendIdealControlMessage(pdcchMsg);
	}
	delete pdcchMsg;
}

void PROPOSEDDownlinkPacketScheduler::DoStopSchedule(void)
{
#ifdef SCHEDULER_DEBUG
	std::cout << "\t PROPOSED Creating Packet Burst" << std::endl;
#endif

	PacketBurst *pb = new PacketBurst();

	//Create Packet Burst
	FlowsToSchedule *flowsToSchedule = GetFlowsToSchedule();

	for (FlowsToSchedule::iterator it = flowsToSchedule->begin(); it != flowsToSchedule->end(); it++)
	{
		FlowToSchedule *flow = (*it);

		int availableBytes = flow->GetAllocatedBits() / 8;

		if (availableBytes > 0)
		{

			flow->GetBearer()->UpdateTransmittedBytes(availableBytes);

#ifdef SCHEDULER_DEBUG
			std::cout << "\t  --> add packets for flow "
					  << flow->GetBearer()->GetApplication()->GetApplicationID() << std::endl;
			std::cout << "\t TransmittedBytes: " << flow->GetBearer()->GetTransmittedBytes() << std::endl;
#endif

			RlcEntity *rlc = flow->GetBearer()->GetRlcEntity();
			PacketBurst *pb2 = rlc->TransmissionProcedure(availableBytes);

#ifdef SCHEDULER_DEBUG
			std::cout << "\t\t  nb of packets: " << pb2->GetNPackets() << std::endl;
#endif

			if (pb2->GetNPackets() > 0)
			{
				std::list<Packet *> packets = pb2->GetPackets();
				std::list<Packet *>::iterator it;
				for (it = packets.begin(); it != packets.end(); it++)
				{
#ifdef SCHEDULER_DEBUG
					std::cout << "\t\t  added packet of bytes " << (*it)->GetSize() << std::endl;
					//(*it)->Print ();
#endif

					Packet *p = (*it);
					pb->AddPacket(p->Copy());
				}
			}
			delete pb2;
		}
		else
		{
		}
	}

	//UpdateAverageTransmissionRate ();

	//SEND PACKET BURST

#ifdef SCHEDULER_DEBUG
	if (pb->GetNPackets() == 0)
		std::cout << "\t Send only reference symbols" << std::endl;
#endif

	GetMacEntity()->GetDevice()->SendPacketBurst(pb);
}